"""Bots: built-in @BotFather, Bot API (/bot<token>/<method>, Telegram-style: getUpdates long polling, webhooks, send*/edit*/delete*, inline keyboards, callback queries,
inline mode, Mini Apps (WebApp initData), Stars invoices) and the user-side endpoints that talk to bots.
Bots work in chats with the bot, public groups and public channels (plaintext). They cannot join end-to-end encrypted chats."""
import hashlib, hmac, ipaddress, json, logging, re, secrets, socket, threading, time, urllib.parse, urllib.request
from pathlib import Path

import ext_media, ext_msgs, ext_stars
from api import ApiError, NEWS, emit, get_member, hash_key, notify, now, raw, route, s, user_by_login
from ext_msgs import SEL, full_msg, jl, post_message, wipe

log = logging.getLogger("mm.bots")
BOT_RE = re.compile(r"[A-Za-z][A-Za-z0-9_]{3,30}[Bb]ot")


# ------------------------------------------------------------------ bot accounts
def bot_by_token(app, token):
    return app.db.one("select * from users where bot_token=? and is_bot=1 and deleted=0 and banned=0", (token,)) if token else None


def create_bot(app, owner, name, username):
    if not BOT_RE.fullmatch(username):
        raise ApiError(400, "bad_username", "Имя бота: 5–32 символа, латиница/цифры/_, должно заканчиваться на «bot»")
    from api import name_taken
    if name_taken(app, username):
        raise ApiError(409, "username_taken", "Это имя уже занято")
    if app.db.one("select count(*) n from users where bot_owner=? and is_bot=1 and deleted=0", (owner["id"],))["n"] >= int(app.cfg.get("bots", "max_per_user", 20)):
        raise ApiError(400, "bots_limit", "Достигнут лимит ботов")
    salt = secrets.token_bytes(16)
    login = "bot_" + secrets.token_hex(6)
    uid = app.db.x("insert into users(login,username,name,auth,salt,created,is_bot,bot_owner,pub) values(?,?,?,?,?,?,1,?,?)",
                   (login, username, s(name, 40) or username, hash_key(secrets.token_hex(32), salt), salt, now(), owner["id"], ""))
    token = "%d:%s" % (uid, secrets.token_urlsafe(24))
    app.db.x("update users set bot_token=? where id=?", (token, uid))
    return app.db.one("select * from users where id=?", (uid,)), token


def ensure_botfather(app):
    r = app.db.one("select * from users where username='BotFather' and is_bot=1")
    if r:
        return r
    salt = secrets.token_bytes(16)
    uid = app.db.x("insert into users(login,username,name,auth,salt,created,is_bot,bot_about,bot_inline,pub) values('botfather_sys','BotFather','BotFather',?,?,?,1,?,0,'')",
                   (hash_key(secrets.token_hex(32), salt), salt, now(), "Создавайте и настраивайте ботов"))
    app.db.x("update users set bot_token=? where id=?", ("%d:%s" % (uid, secrets.token_urlsafe(24)), uid))
    return app.db.one("select * from users where id=?", (uid,))


# ------------------------------------------------------------------ Telegram-like JSON
def tg_user(u):
    return {"id": u["id"], "is_bot": bool(u["is_bot"]), "first_name": u["name"], "username": u["username"] or u["login"]}


def tg_chat(ch, bot_id=0, app=None):
    t = {"private": "private", "bot": "private", "group": "supergroup", "channel": "channel", "secret": "private"}[ch["kind"]]
    d = {"id": ch["id"], "type": t, "title": ch["title"] or None, "username": ch["username"]}
    return {k: v for k, v in d.items() if v}


MEDIA_KEYS = {"photo": "photo", "video": "video", "file": "document", "voice": "voice", "audio": "audio", "gif": "animation", "sticker": "sticker", "round": "video_note"}


def tg_message(app, r):
    ch = app.db.one("select * from chats where id=?", (r["chat_id"],))
    sender = app.db.one("select * from users where id=?", (r["sender_id"],))
    d = {"message_id": r["id"], "from": tg_user(sender), "chat": tg_chat(ch), "date": r["ts"]}
    if r["media_id"]:
        m = app.db.one("select * from media where id=?", (r["media_id"],))
        if m:
            f = {"file_id": str(m["id"]), "file_size": m["size"], "file_name": m["name"], "mime_type": m["mime"], "width": m["w"], "height": m["h"], "duration": m["dur"]}
            d[MEDIA_KEYS.get(r["kind"], "document")] = [f] if r["kind"] == "photo" else f
        if r["body"]:
            d["caption"] = r["body"]
    elif r["kind"] == "poll":
        d["poll"] = ext_msgs.poll_dict(app, r["id"], 0)
    else:
        d["text"] = r["body"]
    if r["reply_to"]:
        rr = app.db.one(SEL + " where m.id=?", (r["reply_to"],))
        if rr:
            d["reply_to_message"] = {"message_id": rr["id"], "from": tg_user(app.db.one("select * from users where id=?", (rr["sender_id"],))), "text": rr["body"], "date": rr["ts"]}
    if r["edited_at"]:
        d["edit_date"] = r["edited_at"]
    if r["via_bot"]:
        d["via_bot"] = tg_user(app.db.one("select * from users where id=?", (r["via_bot"],)))
    if r["markup"]:
        d["reply_markup"] = jl(r["markup"])
    if r["kind"] == "invoice":
        d["invoice"] = jl(r["meta"])
    return d


def bot_push(app, bot_id, kind, payload):
    app.db.x("insert into bot_updates(bot_id,data,ts) values(?,?,?)", (bot_id, json.dumps({kind: payload}, ensure_ascii=False), now()))
    app.db.x("delete from bot_updates where bot_id=? and id<=(select coalesce(max(id),0)-?5000 from bot_updates where bot_id=?)", (bot_id, 0, bot_id)) if False else None
    notify()


# ------------------------------------------------------------------ routing of chat messages to bots
def on_message(app, ch, row, sender):
    if sender["is_bot"]:
        return
    if ch["kind"] == "bot":
        bot = app.db.one("select u.* from members m join users u on u.id=m.user_id where m.chat_id=? and u.is_bot=1", (ch["id"],))
        if bot and bot["username"] == "BotFather":
            return botfather(app, sender, ch, bot, row["body"])
        if bot:
            bot_push(app, bot["id"], "message", tg_message(app, row))
        return
    if not ch["public"] or ch["kind"] not in ("group", "channel"):
        return
    for bot in app.db.q("select u.* from members m join users u on u.id=m.user_id where m.chat_id=? and u.is_bot=1", (ch["id"],)):
        text = row["body"] or ""
        replied = bool(row["reply_to"]) and app.db.one("select 1 from messages where id=? and sender_id=?", (row["reply_to"], bot["id"])) is not None
        if ch["kind"] == "channel":
            bot_push(app, bot["id"], "channel_post", tg_message(app, row))
        elif not bot["bot_privacy"] or text.startswith("/") or ("@" + (bot["username"] or "").lower()) in text.lower() or replied:
            bot_push(app, bot["id"], "message", tg_message(app, row))


def on_edit(app, ch, row, sender):
    if sender["is_bot"]:
        return
    for bot in app.db.q("select u.* from members m join users u on u.id=m.user_id where m.chat_id=? and u.is_bot=1", (ch["id"],)):
        if bot["username"] != "BotFather" and (ch["kind"] == "bot" or ch["public"]):
            bot_push(app, bot["id"], "edited_message", tg_message(app, row))


def on_started(app, ch, user, bot):
    if bot["username"] == "BotFather":
        bot_send(app, bot, ch["id"], BF_HELP)


def on_added(app, ch, bot, by):
    bot_push(app, bot["id"], "my_chat_member", {"chat": tg_chat(ch), "from": tg_user(by), "date": now(), "new_chat_member": {"status": "member", "user": tg_user(bot)}})


def bot_send(app, bot, chat_id, text, **kw):
    ch, mem = get_member(app, chat_id, bot["id"])
    p = {"body": text}
    p.update(kw)
    mid = post_message(app, bot, ch, mem, p)
    return app.db.one(SEL + " where m.id=?", (mid,))


# ------------------------------------------------------------------ @BotFather
BF_HELP = ("Я помогу создать и настроить ботов.\n\n/newbot — создать бота\n/mybots — мои боты\n/token @бот — токен\n/revoke @бот — новый токен\n"
           "/setname @бот Имя\n/setdescription @бот Описание\n/setcommands @бот (следующим сообщением: «команда - описание», по строке)\n"
           "/setinline @бот on|off\n/setprivacy @бот enable|disable\n/setwebapp @бот https://адрес\n/deletebot @бот")


def my_bot(app, owner, ref):
    b = app.db.one("select * from users where username=? and is_bot=1 and bot_owner=? and deleted=0", (ref.lstrip("@"), owner["id"]))
    if not b:
        raise ApiError(404, "no_bot", "Бот %s не найден среди ваших" % ref)
    return b


def set_commands(app, bot_id, lines):
    app.db.x("delete from bot_commands where bot_id=?", (bot_id,))
    for ln in lines:
        m = re.match(r"/?([a-z0-9_]{1,32})\s*[-–:]\s*(.{1,256})$", ln.strip(), re.I)
        if m:
            app.db.x("insert or replace into bot_commands(bot_id,command,description) values(?,?,?)", (bot_id, m.group(1).lower(), m.group(2)))


def botfather(app, user, ch, bf, text):
    st = app.bf_state.pop(user["id"], None)
    reply = lambda t, **kw: bot_send(app, bf, ch["id"], t, **kw)
    t = (text or "").strip()
    try:
        if t.startswith("/") and st:
            st = None
        if st and st[0] == "name":
            app.bf_state[user["id"]] = ("username", t[:40])
            return reply("Хорошо. Теперь выберите имя пользователя (латиница, оканчивается на «bot», например my_shop_bot).")
        if st and st[0] == "username":
            try:
                b, token = create_bot(app, user, st[1], t.lstrip("@"))
            except ApiError as e:
                app.bf_state[user["id"]] = st
                return reply(e.message + "\nПопробуйте другое имя.")
            return reply("Готово! Бот @%s создан.\n\nТокен:\n%s\n\nХраните токен в секрете: он даёт полный доступ к боту." % (b["username"], token))
        if st and st[0] == "commands":
            set_commands(app, st[1], t.splitlines())
            return reply("Список команд обновлён.")
        parts = t.split(None, 2)
        cmd = parts[0].lower() if parts else ""
        if cmd in ("/start", "/help"):
            return reply(BF_HELP)
        if cmd == "/newbot":
            app.bf_state[user["id"]] = ("name", "")
            return reply("Как назвать бота? Это имя увидят пользователи.")
        if cmd == "/mybots":
            rows = app.db.q("select username from users where is_bot=1 and bot_owner=? and deleted=0", (user["id"],))
            return reply("Ваши боты:\n" + "\n".join("@" + r["username"] for r in rows) if rows else "У вас пока нет ботов. /newbot")
        if len(parts) < 2 and cmd != "/newbot":
            return reply("Укажите бота: %s @имя_бота" % cmd) if cmd.startswith("/") else reply(BF_HELP)
        b = my_bot(app, user, parts[1]) if cmd in ("/token", "/revoke", "/setname", "/setdescription", "/setcommands", "/setinline", "/setprivacy", "/setwebapp", "/deletebot") else None
        rest = parts[2] if len(parts) > 2 else ""
        if cmd == "/token":
            return reply("Токен @%s:\n%s" % (b["username"], b["bot_token"]))
        if cmd == "/revoke":
            tok = "%d:%s" % (b["id"], secrets.token_urlsafe(24))
            app.db.x("update users set bot_token=? where id=?", (tok, b["id"]))
            return reply("Новый токен:\n" + tok)
        if cmd == "/setname":
            app.db.x("update users set name=? where id=?", (rest[:40] or b["name"], b["id"]))
            return reply("Имя изменено.")
        if cmd == "/setdescription":
            app.db.x("update users set bot_about=? where id=?", (rest[:200], b["id"]))
            return reply("Описание изменено.")
        if cmd == "/setcommands":
            if rest:
                set_commands(app, b["id"], rest.splitlines())
                return reply("Список команд обновлён.")
            app.bf_state[user["id"]] = ("commands", b["id"])
            return reply("Отправьте список команд, по строке: «команда - описание».")
        if cmd == "/setinline":
            app.db.x("update users set bot_inline=? where id=?", (int(rest.strip().lower() in ("on", "1", "yes", "enable")), b["id"]))
            return reply("Инлайн-режим обновлён.")
        if cmd == "/setprivacy":
            app.db.x("update users set bot_privacy=? where id=?", (int(rest.strip().lower() in ("enable", "on", "1")), b["id"]))
            return reply("Режим приватности обновлён.")
        if cmd == "/setwebapp":
            app.db.x("update users set webapp_url=? where id=?", (rest.strip() if rest.strip().startswith(("https://", "http://")) else "", b["id"]))
            return reply("Адрес мини-приложения обновлён.")
        if cmd == "/deletebot":
            app.db.x("update users set deleted=1, bot_token=NULL, username=?, name='Удалённый бот' where id=?", ("deleted_%d" % b["id"], b["id"]))
            return reply("Бот удалён.")
        return reply(BF_HELP)
    except ApiError as e:
        return reply(e.message)


# ------------------------------------------------------------------ Bot API
METHODS = {}


def method(name=None, *aliases):
    def deco(f):
        for n in (name or f.__name__[2:],) + aliases:
            METHODS[n] = f
        return f
    return deco


def wait_for(app, key, timeout):
    ev = app.waiters.setdefault(key, {"ev": threading.Event(), "res": None})
    ev["ev"].wait(timeout)
    return app.waiters.pop(key, ev)["res"]


def answer(app, key, res):
    w = app.waiters.setdefault(key, {"ev": threading.Event(), "res": None})
    w["res"] = res
    w["ev"].set()


def chat_of(app, bot, p, key="chat_id"):
    v = p.get(key)
    if isinstance(v, str) and v.startswith("@"):
        ch = app.db.one("select * from chats where username=?", (v[1:],))
    elif v is not None and str(v).lstrip("-").isdigit():
        ch = app.db.one("select * from chats where id=?", (int(v),))
        if not ch or not app.db.one("select 1 from members where chat_id=? and user_id=?", (ch["id"], bot["id"])):   # Telegram style: chat_id = user id
            ch = app.db.one("select c.* from chats c join members a on a.chat_id=c.id and a.user_id=? join members b on b.chat_id=c.id and b.user_id=? where c.kind='bot'", (bot["id"], int(v)))
    else:
        ch = None
    if not ch:
        raise ApiError(400, "bad_chat", "Bad Request: chat not found")
    mem = app.db.one("select * from members where chat_id=? and user_id=?", (ch["id"], bot["id"]))
    if not mem:
        raise ApiError(403, "forbidden", "Forbidden: bot is not a member of the chat")
    return ch, mem


def send_generic(app, bot, p, kind="text", media_key=None, extra=None):
    ch, mem = chat_of(app, bot, p)
    body = s(p.get("text") if kind == "text" else p.get("caption"), 60000) if (p.get("text") or p.get("caption")) else ""
    q = {"body": body, "kind": kind, "reply_to": p.get("reply_to_message_id") or 0, "markup": p.get("reply_markup") if isinstance(p.get("reply_markup"), dict) else None,
         "meta": {"parse_mode": p["parse_mode"]} if p.get("parse_mode") else {}}
    if media_key:
        q["media_id"] = int(str(p.get(media_key) or 0))
    q.update(extra or {})
    mid = post_message(app, bot, ch, mem, q)
    return tg_message(app, app.db.one(SEL + " where m.id=?", (mid,)))


@method()
def m_getMe(app, bot, p, h):
    return dict(tg_user(bot), can_join_groups=True, can_read_all_group_messages=not bot["bot_privacy"], supports_inline_queries=bool(bot["bot_inline"]))


@method()
def m_sendMessage(app, bot, p, h):
    if not s(p.get("text"), 10):
        raise ApiError(400, "bad_request", "Bad Request: message text is empty")
    return send_generic(app, bot, p)


for _n, _k, _m in (("sendPhoto", "photo", "photo"), ("sendVideo", "video", "video"), ("sendDocument", "file", "document"), ("sendVoice", "voice", "voice"), ("sendAudio", "audio", "audio"),
                   ("sendAnimation", "gif", "animation"), ("sendSticker", "sticker", "sticker"), ("sendVideoNote", "round", "video_note")):
    METHODS[_n] = (lambda kind, key: lambda app, bot, p, h: send_generic(app, bot, p, kind, key))(_k, _m)


@method()
def m_sendPoll(app, bot, p, h):
    poll = {"question": p.get("question"), "options": [o if isinstance(o, str) else o.get("text") for o in p.get("options", [])], "anonymous": p.get("is_anonymous", True),
            "multiple": p.get("allows_multiple_answers", False), "quiz": p.get("type") == "quiz", "correct": p.get("correct_option_id", -1), "explanation": p.get("explanation", "")}
    return send_generic(app, bot, p, "poll", None, {"poll": poll, "body": p.get("question")})


@method()
def m_editMessageText(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    msg = app.db.one("select * from messages where id=? and chat_id=? and sender_id=? and deleted=0", (int(p.get("message_id") or 0), ch["id"], bot["id"]))
    if not msg or not s(p.get("text"), 10):
        raise ApiError(400, "bad_request", "Bad Request: message can't be edited")
    mk = p.get("reply_markup") if isinstance(p.get("reply_markup"), dict) else None
    app.db.x("update messages set body=?, edited_at=?, markup=coalesce(?, markup) where id=?", (p["text"], now(), json.dumps(mk) if mk else None, msg["id"]))
    emit(app, ch["id"], "edit", {"msg_id": msg["id"], "body": p["text"], "enc": 0, "edited_at": now(), "markup": mk})
    return tg_message(app, app.db.one(SEL + " where m.id=?", (msg["id"],)))


METHODS["editMessageCaption"] = METHODS["editMessageText"]


@method()
def m_editMessageReplyMarkup(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    msg = app.db.one("select * from messages where id=? and chat_id=? and sender_id=?", (int(p.get("message_id") or 0), ch["id"], bot["id"]))
    if not msg:
        raise ApiError(400, "bad_request", "Bad Request: message not found")
    mk = p.get("reply_markup") if isinstance(p.get("reply_markup"), dict) else {}
    app.db.x("update messages set markup=? where id=?", (json.dumps(mk), msg["id"]))
    emit(app, ch["id"], "edit", {"msg_id": msg["id"], "body": msg["body"], "enc": 0, "edited_at": msg["edited_at"], "markup": mk})
    return True


@method()
def m_deleteMessage(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    msg = app.db.one("select * from messages where id=? and chat_id=? and deleted=0", (int(p.get("message_id") or 0), ch["id"]))
    if not msg or (msg["sender_id"] != bot["id"] and mem["role"] not in ("owner", "admin")):
        raise ApiError(400, "bad_request", "Bad Request: message can't be deleted")
    wipe(app, ch, [msg["id"]])
    return True


@method()
def m_forwardMessage(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    src, smem = chat_of(app, bot, {"chat_id": p.get("from_chat_id")})
    msg = app.db.one("select * from messages where id=? and chat_id=? and deleted=0", (int(p.get("message_id") or 0), src["id"]))
    if not msg or src["noforwards"]:
        raise ApiError(400, "bad_request", "Bad Request: message can't be forwarded")
    sender = app.db.one("select * from users where id=?", (msg["sender_id"],))
    return send_generic(app, bot, p, msg["kind"] if msg["kind"] in ext_msgs.KINDS else "text", None,
                        {"body": msg["body"], "media_id": msg["media_id"], "fwd": {"from_id": sender["id"], "from_name": sender["name"], "chat_id": src["id"], "msg_id": msg["id"], "date": msg["ts"]}})


@method()
def m_sendChatAction(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    for r in app.db.q("select user_id from members where chat_id=? and user_id!=? limit 100", (ch["id"], bot["id"])):
        ext_msgs.push_eph(app, r["user_id"], {"type": "typing", "chat_id": ch["id"], "user_id": bot["id"], "username": bot["username"], "action": p.get("action", "typing"), "ts": now()})
    return True


@method()
def m_getChat(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    return dict(tg_chat(ch), description=ch["about"], members=app.db.one("select count(*) n from members where chat_id=?", (ch["id"],))["n"])


@method()
def m_getChatMemberCount(app, bot, p, h):
    return m_getChat(app, bot, p, h)["members"]


@method()
def m_getChatMember(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    r = app.db.one("select u.*, m.role from members m join users u on u.id=m.user_id where m.chat_id=? and m.user_id=?", (ch["id"], int(p.get("user_id") or 0)))
    return {"status": {"owner": "creator", "admin": "administrator"}.get(r["role"], "member"), "user": tg_user(r)} if r else {"status": "left", "user": {"id": int(p.get("user_id") or 0)}}


@method()
def m_getChatAdministrators(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    return [{"status": "creator" if r["role"] == "owner" else "administrator", "user": tg_user(r)} for r in
            app.db.q("select u.*, m.role from members m join users u on u.id=m.user_id where m.chat_id=? and m.role in ('owner','admin')", (ch["id"],))]


@method()
def m_banChatMember(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    if mem["role"] not in ("owner", "admin") or not ch["public"]:
        raise ApiError(403, "forbidden", "Forbidden: not enough rights")
    app.db.x("delete from members where chat_id=? and user_id=? and role not in ('owner','admin')", (ch["id"], int(p.get("user_id") or 0)))
    emit(app, ch["id"], "member_removed", {"user_id": int(p.get("user_id") or 0)})
    return True


METHODS["unbanChatMember"] = lambda app, bot, p, h: True


@method()
def m_leaveChat(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    app.db.x("delete from members where chat_id=? and user_id=?", (ch["id"], bot["id"]))
    return True


@method()
def m_pinChatMessage(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    if mem["role"] not in ("owner", "admin") and ch["kind"] != "bot":
        raise ApiError(403, "forbidden", "Forbidden: not enough rights")
    app.db.x("update chats set pinned_msg=? where id=?", (int(p.get("message_id") or 0), ch["id"]))
    emit(app, ch["id"], "pin", {"msg_id": int(p.get("message_id") or 0), "by": bot["id"]})
    return True


METHODS["unpinChatMessage"] = lambda app, bot, p, h: m_pinChatMessage(app, bot, dict(p, message_id=0), h)


@method()
def m_setMessageReaction(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    app.db.x("delete from reactions where msg_id=? and user_id=?", (int(p.get("message_id") or 0), bot["id"]))
    for r in (p.get("reaction") or [])[:1]:
        app.db.x("insert or ignore into reactions(msg_id,user_id,emoji,ts) values(?,?,?,?)", (int(p["message_id"]), bot["id"], r.get("emoji", "👍") if isinstance(r, dict) else str(r), now()))
    return True


@method()
def m_stopPoll(app, bot, p, h):
    ch, mem = chat_of(app, bot, p)
    app.db.x("update polls set closed=1 where msg_id=?", (int(p.get("message_id") or 0),))
    return ext_msgs.poll_dict(app, int(p.get("message_id") or 0), 0)


# ---- updates
@method()
def m_getUpdates(app, bot, p, h):
    if app.db.one("select 1 from webhooks where bot_id=?", (bot["id"],)):
        raise ApiError(409, "conflict", "Conflict: can't use getUpdates while a webhook is active")
    off, limit, timeout = int(p.get("offset") or 0), max(1, min(int(p.get("limit") or 100), 100)), min(int(p.get("timeout") or 0), 50)
    if off > 0:
        app.db.x("delete from bot_updates where bot_id=? and id<?", (bot["id"], off))
    deadline = time.time() + timeout
    while True:
        rows = app.db.q("select * from bot_updates where bot_id=? and id>=? order by id limit ?", (bot["id"], off, limit))
        if rows or time.time() >= deadline:
            return [dict(json.loads(r["data"]), update_id=r["id"]) for r in rows]
        with NEWS:
            NEWS.wait(timeout=min(2.0, max(0.1, deadline - time.time())))


def url_allowed(app, url):
    u = urllib.parse.urlparse(url)
    if u.scheme not in ("http", "https") or not u.hostname:
        return False
    if app.cfg.get("bots", "allow_private_webhooks", False):
        return True
    try:
        for fam, _, _, _, sa in socket.getaddrinfo(u.hostname, u.port or 80):
            ip = ipaddress.ip_address(sa[0])
            if ip.is_private or ip.is_loopback or ip.is_link_local or ip.is_reserved:
                return False
    except OSError:
        return False
    return True


@method()
def m_setWebhook(app, bot, p, h):
    url = s(p.get("url"), 500)
    if not url:
        return m_deleteWebhook(app, bot, p, h)
    if not url_allowed(app, url):
        raise ApiError(400, "bad_webhook", "Bad Request: bad webhook URL (private addresses are not allowed)")
    app.db.x("insert or replace into webhooks(bot_id,url,secret,failures,last_error) values(?,?,?,0,'')", (bot["id"], url, s(p.get("secret_token"), 256)))
    if p.get("drop_pending_updates"):
        app.db.x("delete from bot_updates where bot_id=?", (bot["id"],))
    return True


@method()
def m_deleteWebhook(app, bot, p, h):
    app.db.x("delete from webhooks where bot_id=?", (bot["id"],))
    if p.get("drop_pending_updates"):
        app.db.x("delete from bot_updates where bot_id=?", (bot["id"],))
    return True


@method()
def m_getWebhookInfo(app, bot, p, h):
    w = app.db.one("select * from webhooks where bot_id=?", (bot["id"],))
    return {"url": w["url"] if w else "", "pending_update_count": app.db.one("select count(*) n from bot_updates where bot_id=?", (bot["id"],))["n"],
            "last_error_message": w["last_error"] if w else ""}


def webhook_tick(app):
    for w in app.db.q("select * from webhooks"):
        if app.wh_next.get(w["bot_id"], 0) > time.time():
            continue
        for r in app.db.q("select * from bot_updates where bot_id=? order by id limit 20", (w["bot_id"],)):
            body = json.dumps(dict(json.loads(r["data"]), update_id=r["id"])).encode()
            req = urllib.request.Request(w["url"], data=body, headers={"Content-Type": "application/json", "X-Telegram-Bot-Api-Secret-Token": w["secret"]})
            try:
                with urllib.request.urlopen(req, timeout=10) as resp:
                    ok = 200 <= resp.status < 300
            except Exception as e:
                ok, err = False, str(e)[:200]
            if ok:
                app.db.x("delete from bot_updates where id=?", (r["id"],))
                app.db.x("update webhooks set failures=0,last_error='' where bot_id=?", (w["bot_id"],))
            else:
                f = w["failures"] + 1
                app.db.x("update webhooks set failures=?, last_error=? where bot_id=?", (f, locals().get("err", "HTTP error"), w["bot_id"]))
                app.wh_next[w["bot_id"]] = time.time() + min(2 ** f, 300)
                break


# ---- profile / menu
@method()
def m_setMyCommands(app, bot, p, h):
    app.db.x("delete from bot_commands where bot_id=?", (bot["id"],))
    for c in (p.get("commands") or [])[:100]:
        app.db.x("insert or replace into bot_commands(bot_id,command,description) values(?,?,?)", (bot["id"], s(c.get("command"), 32).lower(), s(c.get("description"), 256)))
    return True


@method()
def m_getMyCommands(app, bot, p, h):
    return [{"command": r["command"], "description": r["description"]} for r in app.db.q("select * from bot_commands where bot_id=? order by rowid", (bot["id"],))]


METHODS["deleteMyCommands"] = lambda app, bot, p, h: (app.db.x("delete from bot_commands where bot_id=?", (bot["id"],)), True)[1]


@method()
def m_setMyName(app, bot, p, h):
    app.db.x("update users set name=? where id=?", (s(p.get("name"), 40) or bot["name"], bot["id"]))
    return True


@method()
def m_setMyDescription(app, bot, p, h):
    app.db.x("update users set bot_about=? where id=?", (s(p.get("description"), 200), bot["id"]))
    return True


@method()
def m_setChatMenuButton(app, bot, p, h):
    mb = p.get("menu_button") or {}
    url = (mb.get("web_app") or {}).get("url", "") if mb.get("type") == "web_app" else ""
    if url and not url.startswith(("https://", "http://")):
        raise ApiError(400, "bad_request", "Bad Request: invalid web app URL")
    app.db.x("update users set webapp_url=? where id=?", (url, bot["id"]))
    return True


@method()
def m_getChatMenuButton(app, bot, p, h):
    return {"type": "web_app", "text": "Open", "web_app": {"url": bot["webapp_url"]}} if bot["webapp_url"] else {"type": "default"}


# ---- callbacks, inline, webapp, files
@method()
def m_answerCallbackQuery(app, bot, p, h):
    answer(app, ("cb", s(p.get("callback_query_id"), 40)), {"text": s(p.get("text"), 200), "show_alert": bool(p.get("show_alert")), "url": s(p.get("url"), 500)})
    return True


@method()
def m_answerInlineQuery(app, bot, p, h):
    answer(app, ("iq", s(p.get("inline_query_id"), 40)), {"results": (p.get("results") or [])[:50], "next_offset": s(p.get("next_offset"), 64), "cache_time": int(p.get("cache_time") or 300)})
    return True


@method()
def m_answerWebAppQuery(app, bot, p, h):
    q = app.db.one("select * from inline_results where id=? and bot_id=?", (s(p.get("web_app_query_id"), 40), bot["id"]))
    if not q:
        raise ApiError(400, "bad_request", "Bad Request: query is invalid or expired")
    info = json.loads(q["data"])
    res = p.get("result") or {}
    ch = app.db.one("select * from chats where id=?", (info["chat_id"],)) if info.get("chat_id") else None
    if not ch:
        raise ApiError(400, "bad_request", "Bad Request: no chat for this query")
    text = res.get("message_text") or (res.get("input_message_content") or {}).get("message_text") or res.get("title") or ""
    app.db.x("delete from inline_results where id=?", (q["id"],))
    return {"inline_message_id": bot_send(app, bot, ch["id"], text)["id"]}


@method()
def m_getFile(app, bot, p, h):
    m = app.db.one("select * from media where id=? and ready=1", (int(str(p.get("file_id") or 0)),))
    if not m or not bot_can_read(app, bot, m):
        raise ApiError(400, "bad_request", "Bad Request: file not found")
    return {"file_id": str(m["id"]), "file_size": m["size"], "file_path": str(m["id"])}


def bot_can_read(app, bot, m):
    return ext_media.user_can_read_media(app, bot, m)


@raw("GET", "/file/bot")
def bot_file(h, app, c, path):
    mt = re.match(r"/file/bot(\d+:[A-Za-z0-9_-]+)/(\d+)$", path)
    bot = bot_by_token(app, mt.group(1)) if mt else None
    m = app.db.one("select * from media where id=? and ready=1", (int(mt.group(2)),)) if bot and mt else None
    if not m or not bot_can_read(app, bot, m) or not Path(m["path"]).exists():
        raise ApiError(404, "not_found", "Not Found")
    h.send_file(m["path"], "application/octet-stream" if m["enc"] else m["mime"], m["name"], m["size"])


# ---- Stars invoices
@method()
def m_sendInvoice(app, bot, p, h):
    prices = p.get("prices") or []
    total = sum(int(x.get("amount") or 0) for x in prices)
    if p.get("currency") != "XTR" or not 1 <= total <= 10000 or not s(p.get("title"), 5) or not p.get("payload"):
        raise ApiError(400, "bad_request", "Bad Request: currency must be XTR, total 1..10000, title and payload required")
    meta = {"title": s(p["title"], 32), "description": s(p.get("description"), 255), "currency": "XTR", "total_amount": total, "prices": prices, "photo_media_id": int(p.get("photo_media_id") or 0)}
    ch, mem = chat_of(app, bot, p)
    out = send_generic(app, bot, p, "invoice", None, {"body": meta["title"], "meta": meta, "markup": {"inline_keyboard": [[{"text": "Оплатить ⭐%d" % total, "pay": True}]]}})
    app.db.x("insert into invoices(msg_id,payload,currency,total,title) values(?,?,?,?,?)", (out["message_id"], s(p["payload"], 128), "XTR", total, meta["title"]))
    return out


@method()
def m_answerPreCheckoutQuery(app, bot, p, h):
    answer(app, ("pc", s(p.get("pre_checkout_query_id"), 40)), {"ok": bool(p.get("ok")), "error": s(p.get("error_message"), 200)})
    return True


@method()
def m_refundStarPayment(app, bot, p, h):
    cid = s(p.get("telegram_payment_charge_id"), 64)
    tx = app.db.one("select * from stars_tx where kind='invoice_in' and ref=? and user_id=?", (cid, bot["bot_owner"]))
    inv = app.db.one("select * from invoices where charge_id=? and paid=1", (cid,))
    if not tx or not inv or inv["paid_by"] != int(p.get("user_id") or 0):
        raise ApiError(400, "bad_request", "Bad Request: charge not found")
    ext_stars.add_stars(app, bot["bot_owner"], -tx["amount"], "invoice_refund_out", cid, "Возврат платежа", need=True)
    ext_stars.add_stars(app, inv["paid_by"], inv["total"], "invoice_refund", cid, "Возврат платежа")
    app.db.x("update invoices set paid=2 where msg_id=?", (inv["msg_id"],))
    return True


@raw("GET", "/bot")
def bot_api_get(h, app, c, path):
    return bot_api(h, app, c, path)


@raw("POST", "/bot")
def bot_api_post(h, app, c, path):
    return bot_api(h, app, c, path)


def bot_api(h, app, c, path):
    mt = re.match(r"/bot(\d+:[A-Za-z0-9_-]+)/(\w+)$", path)
    if not mt:
        raise ApiError(404, "not_found", "unknown endpoint")
    bot = bot_by_token(app, mt.group(1))
    if not bot:
        return h._json(401, {"ok": False, "error_code": 401, "description": "Unauthorized"})
    name = mt.group(2)
    try:
        if name == "uploadFile":      # raw upload as the bot: POST /bot<token>/uploadFile?kind=&name=&mime=  (binary body)
            c.u = bot
            q = dict(c.q)
            n = int(h.headers.get("Content-Length") or 0)
            mid = ext_media._new(app, bot, q)
            p = ext_media.media_path(app, mid)
            sha = ext_media._stream(h, p, n)
            app.db.x("update media set size=?, path=?, sha256=?, ready=1 where id=?", (n, str(p), sha.hexdigest(), mid))
            return h._json(200, {"ok": True, "result": {"file_id": str(mid), "file_size": n}})
        params = dict(c.q)
        if h.command == "POST":
            params.update(h.read_json())
        fn = METHODS.get(name)
        if not fn:
            return h._json(404, {"ok": False, "error_code": 404, "description": "Not Found: method not found"})
        h._json(200, {"ok": True, "result": fn(app, bot, params, h)})
    except ApiError as e:
        h._json(e.code if e.code < 500 else 400, {"ok": False, "error_code": e.code, "description": e.message})


# ------------------------------------------------------------------ user-side: bot management, callbacks, inline, Mini Apps, invoices
def bot_row(app, ref):
    b = app.db.one("select * from users where (username=? or id=?) and is_bot=1 and deleted=0", (str(ref).lstrip("@"), int(ref) if str(ref).isdigit() else -1))
    if not b:
        raise ApiError(404, "no_bot", "Бот не найден")
    return b


@route("POST", "/api/bots/create")
def bots_create(app, c):
    b, token = create_bot(app, c.u, c.body.get("name"), s(c.body.get("username"), 40).lstrip("@"))
    return {"bot": {"id": b["id"], "username": b["username"], "name": b["name"]}, "token": token}


@route("GET", "/api/bots/mine")
def bots_mine(app, c):
    return {"bots": [{"id": b["id"], "username": b["username"], "name": b["name"], "token": b["bot_token"], "inline": bool(b["bot_inline"]), "webapp_url": b["webapp_url"],
                      "privacy_mode": bool(b["bot_privacy"]), "about": b["bot_about"]} for b in app.db.q("select * from users where is_bot=1 and bot_owner=? and deleted=0", (c.u["id"],))]}


@route("POST", "/api/bots/update")
def bots_update(app, c):
    b = my_bot(app, c.u, s(c.body.get("username"), 40))
    p = c.body
    if "name" in p:
        app.db.x("update users set name=? where id=?", (s(p["name"], 40) or b["name"], b["id"]))
    if "about" in p:
        app.db.x("update users set bot_about=? where id=?", (s(p["about"], 200), b["id"]))
    if "inline" in p:
        app.db.x("update users set bot_inline=? where id=?", (int(bool(p["inline"])), b["id"]))
    if "privacy_mode" in p:
        app.db.x("update users set bot_privacy=? where id=?", (int(bool(p["privacy_mode"])), b["id"]))
    if "webapp_url" in p:
        app.db.x("update users set webapp_url=? where id=?", (s(p["webapp_url"], 500) if str(p["webapp_url"]).startswith(("https://", "http://")) else "", b["id"]))
    if "commands" in p:
        set_commands(app, b["id"], ["%s - %s" % (x.get("command"), x.get("description")) for x in p["commands"]])
    if p.get("revoke_token"):
        app.db.x("update users set bot_token=? where id=?", ("%d:%s" % (b["id"], secrets.token_urlsafe(24)), b["id"]))
    return {"ok": True}


@route("POST", "/api/bots/delete")
def bots_delete(app, c):
    b = my_bot(app, c.u, s(c.body.get("username"), 40))
    app.db.x("update users set deleted=1, bot_token=NULL, username=?, name='Удалённый бот' where id=?", ("deleted_%d" % b["id"], b["id"]))
    return {"ok": True}


@route("POST", "/api/bots/callback")
def bots_callback(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    msg = app.db.one(SEL + " where m.id=? and m.chat_id=? and m.deleted=0", (int(c.body.get("msg_id") or 0), ch["id"]))
    data = s(c.body.get("data"), 64)
    mk = jl(msg["markup"]) if msg else None
    if not msg or not mk or not any(b.get("callback_data") == data for row in mk.get("inline_keyboard", []) for b in row):
        raise ApiError(400, "bad_button", "Кнопка не найдена")
    bot = app.db.one("select * from users where id=? and is_bot=1", (msg["via_bot"] or msg["sender_id"],))
    if not bot:
        raise ApiError(400, "no_bot", "Сообщение отправлено не ботом")
    cid = secrets.token_hex(8)
    bot_push(app, bot["id"], "callback_query", {"id": cid, "from": tg_user(c.u), "message": tg_message(app, msg), "chat_instance": str(ch["id"]), "data": data})
    return {"answer": wait_for(app, ("cb", cid), float(app.cfg.get("bots", "callback_timeout", 8)))}


@route("POST", "/api/inline/query")
def inline_query(app, c):
    bot = bot_row(app, c.body.get("bot"))
    if not bot["bot_inline"]:
        raise ApiError(400, "no_inline", "Бот не поддерживает инлайн-режим")
    qid = secrets.token_hex(8)
    ch = app.db.one("select * from chats where id=?", (int(c.body.get("chat_id") or 0),)) if c.body.get("chat_id") else None
    bot_push(app, bot["id"], "inline_query", {"id": qid, "from": tg_user(c.u), "query": s(c.body.get("query"), 256), "offset": s(c.body.get("offset"), 64), "chat_type": tg_chat(ch)["type"] if ch else "sender"})
    res = wait_for(app, ("iq", qid), float(app.cfg.get("bots", "callback_timeout", 8)))
    app.db.x("insert or replace into inline_results(id,bot_id,user_id,data,ts) values(?,?,?,?,?)", (qid, bot["id"], c.u["id"], json.dumps(res or {}), now()))
    return {"query_id": qid, "bot_id": bot["id"], "results": (res or {}).get("results", []), "next_offset": (res or {}).get("next_offset", "")}


@route("POST", "/api/inline/chosen")
def inline_chosen(app, c):
    r = app.db.one("select * from inline_results where id=? and user_id=?", (s(c.body.get("query_id"), 40), c.u["id"]))
    if r:
        bot_push(app, r["bot_id"], "chosen_inline_result", {"result_id": s(c.body.get("result_id"), 64), "from": tg_user(c.u), "query": s(c.body.get("query"), 256)})
    return {"ok": True}


def sign_init_data(token, fields):
    check = "\n".join("%s=%s" % (k, fields[k]) for k in sorted(fields))
    secret = hmac.new(b"WebAppData", token.encode(), hashlib.sha256).digest()
    return hmac.new(secret, check.encode(), hashlib.sha256).hexdigest()


def validate_init_data(token, init_data, max_age=86400):
    """Bot-side check of Mini App initData (same algorithm as Telegram)."""
    f = dict(urllib.parse.parse_qsl(init_data, keep_blank_values=True))
    h = f.pop("hash", "")
    return hmac.compare_digest(h, sign_init_data(token, f)) and now() - int(f.get("auth_date", 0)) <= max_age


@route("POST", "/api/webapp/init")
def webapp_init(app, c):
    bot = bot_row(app, c.body.get("bot"))
    base = urllib.parse.urlparse(bot["webapp_url"])
    url = s(c.body.get("url"), 500) or bot["webapp_url"]
    uu = urllib.parse.urlparse(url)
    if not base.netloc or (uu.scheme, uu.netloc) != (base.scheme, base.netloc):
        raise ApiError(400, "bad_url", "Адрес мини-приложения должен принадлежать боту")
    qid = "AA" + secrets.token_urlsafe(10)
    app.db.x("insert into inline_results(id,bot_id,user_id,data,ts) values(?,?,?,?,?)", (qid, bot["id"], c.u["id"], json.dumps({"chat_id": int(c.body.get("chat_id") or 0)}), now()))
    f = {"query_id": qid, "user": json.dumps({"id": c.u["id"], "first_name": c.u["name"], "username": c.u["username"], "is_premium": c.u["premium_until"] > now()}, separators=(",", ":"), ensure_ascii=False),
         "auth_date": str(now())}
    if c.body.get("start_param"):
        f["start_param"] = s(c.body["start_param"], 64)
    f["hash"] = sign_init_data(bot["bot_token"], f)
    return {"url": url, "init_data": urllib.parse.urlencode(f), "query_id": qid, "bot_id": bot["id"]}


@route("POST", "/api/invoices/pay")
def invoice_pay(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    inv = app.db.one("select * from invoices where msg_id=?", (int(c.body.get("msg_id") or 0),))
    msg = app.db.one(SEL + " where m.id=? and m.chat_id=? and m.deleted=0", (inv["msg_id"] if inv else 0, ch["id"]))
    if not inv or not msg or inv["paid"]:
        raise ApiError(400, "bad_invoice", "Счёт не найден или уже оплачен")
    bot = app.db.one("select * from users where id=?", (msg["sender_id"],))
    pcid = secrets.token_hex(8)
    bot_push(app, bot["id"], "pre_checkout_query", {"id": pcid, "from": tg_user(c.u), "currency": "XTR", "total_amount": inv["total"], "invoice_payload": inv["payload"]})
    res = wait_for(app, ("pc", pcid), float(app.cfg.get("bots", "callback_timeout", 10)))
    if not res or not res["ok"]:
        raise ApiError(409, "declined", (res or {}).get("error") or "Бот не подтвердил платёж")
    charge = "ch_" + secrets.token_hex(8)
    with app.db.lock:
        ext_stars.add_stars(app, c.u["id"], -inv["total"], "invoice_out", charge, "Оплата: " + inv["title"], need=True)
        ext_stars.add_stars(app, bot["bot_owner"] or bot["id"], ext_stars.commission(app, inv["total"]), "invoice_in", charge, "Оплата от @" + c.u["username"])
        app.db.x("update invoices set paid=1, paid_by=?, charge_id=? where msg_id=?", (c.u["id"], charge, inv["msg_id"]))
    sm = dict(tg_message(app, msg), successful_payment={"currency": "XTR", "total_amount": inv["total"], "invoice_payload": inv["payload"], "telegram_payment_charge_id": charge})
    sm["from"] = tg_user(c.u)
    bot_push(app, bot["id"], "message", sm)
    return {"ok": True, "charge_id": charge}
