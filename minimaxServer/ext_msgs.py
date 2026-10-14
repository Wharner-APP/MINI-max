"""Messages: send / edit / delete / react / reply / forward / read marks / typing / pin / polls / scheduled / self-destruct / drafts-aware sync."""
import json, logging, threading, time

from api import (ApiError, EXTRA_CHAT, NEWS, can, chat_dict, emit, get_member, is_blocked, is_contact, is_premium, lim, msg_dict, notify, now, privacy_of, rate,
                 route, s, user_by_login)
import ext_media

log = logging.getLogger("mm.msgs")
SEL = "select m.*,u.username sender,u.name sender_name from messages m join users u on u.id=m.sender_id"
VISIBLE = " and not exists(select 1 from hides h where h.user_id=? and h.msg_id=m.id)"
ACTIONS = ("typing", "voice", "video", "upload_photo", "upload_video", "upload_document", "choose_sticker", "cancel")
KINDS = ("text", "photo", "video", "file", "voice", "round", "audio", "sticker", "gif", "poll", "poll_vote", "location", "contact", "dice", "service", "invoice", "album", "screenshot", "call")


def jl(x):
    try:
        return json.loads(x) if x else None
    except ValueError:
        return None


# ------------------------------------------------------------------ ephemeral queue (typing, call signalling, gift notices)
def push_eph(app, uid, item):
    with app.eph_lock:
        q = app.eph.setdefault(uid, [])
        if item.get("type") == "typing":
            q[:] = [x for x in q if not (x.get("type") == "typing" and x["chat_id"] == item["chat_id"] and x["user_id"] == item["user_id"])]
        q.append(item)
        del q[:-300]
    notify()


def drain_eph(app, uid):
    with app.eph_lock:
        return app.eph.pop(uid, [])


# ------------------------------------------------------------------ message -> dict
def reaction_summary(app, mid, uid):
    rows = app.db.q("select emoji, count(*) n, max(user_id=?) mine from reactions where msg_id=? group by emoji order by n desc, min(ts)", (uid, mid))
    return [{"emoji": r["emoji"], "count": r["n"], "mine": bool(r["mine"])} for r in rows]


def poll_dict(app, mid, uid):
    p = app.db.one("select * from polls where msg_id=?", (mid,))
    if not p:
        return None
    opts = json.loads(p["options"])
    votes = app.db.q("select opt, count(*) n from poll_votes where msg_id=? group by opt", (mid,))
    cnt = {r["opt"]: r["n"] for r in votes}
    mine = [r["opt"] for r in app.db.q("select opt from poll_votes where msg_id=? and user_id=?", (mid, uid))]
    voted = bool(mine) or bool(p["closed"])
    d = {"question": p["question"], "options": [{"text": t, "count": cnt.get(i, 0), "mine": i in mine} for i, t in enumerate(opts)],
         "total_voters": app.db.one("select count(distinct user_id) n from poll_votes where msg_id=?", (mid,))["n"], "multiple": bool(p["multiple"]),
         "anonymous": bool(p["anonymous"]), "quiz": bool(p["quiz"]), "closed": bool(p["closed"]), "closes_at": p["closes_at"]}
    if p["quiz"] and voted:
        d["correct"], d["explanation"] = p["correct"], p["explanation"]
    return d


def is_locked(app, r, uid):
    if not r["paid_stars"] or r["sender_id"] == uid:
        return False
    mem = app.db.one("select role from members where chat_id=? and user_id=?", (r["chat_id"], uid))
    if mem and mem["role"] in ("owner", "admin"):
        return False
    return app.db.one("select 1 from paid_unlocks where user_id=? and msg_id=?", (uid, r["id"])) is None


def full_msg(app, r, uid):
    d = msg_dict(r)
    d.update(kind=r["kind"], sender_id=r["sender_id"], edited_at=r["edited_at"], views=r["views"], ttl=r["ttl"], expires_at=r["expires_at"], via_bot=r["via_bot"],
             group_id=r["group_id"], from_device=r["from_device"], meta=jl(r["meta"]), fwd=jl(r["fwd"]), markup=jl(r["markup"]))
    if is_locked(app, r, uid):
        d.update(body="", locked=True, price=r["paid_stars"], meta=None, media=None)
        return d
    if r["paid_stars"]:
        d["price"] = r["paid_stars"]
    if r["media_id"]:
        m = app.db.one("select * from media where id=?", (r["media_id"],))
        d["media"] = ext_media.media_dict(m) if m else None
    d["reactions"] = reaction_summary(app, r["id"], uid)
    if r["kind"] == "poll":
        d["poll"] = poll_dict(app, r["id"], uid)
    return d


# ------------------------------------------------------------------ sending (shared by users, bots and the scheduler)
def post_message(app, sender, ch, mem, p, device=""):
    db = app.db
    if ch["banned"]:
        raise ApiError(403, "chat_banned", "Чат заблокирован модератором")
    role, kind = mem["role"], ch["kind"]
    if kind == "channel" and role not in ("owner", "admin"):
        raise ApiError(403, "forbidden", "В канале публиковать могут только администраторы")
    body = p.get("body") if isinstance(p.get("body"), str) else ""
    enc = 1 if p.get("enc") else 0
    mkind = s(p.get("kind"), 12) or "text"
    if mkind not in KINDS:
        raise ApiError(400, "bad_kind", "Неизвестный тип сообщения")
    # Private chats prefer E2E, but plaintext is allowed when the client has no peer key yet.
    must_enc = kind == "secret"
    if must_enc and not enc:
        raise ApiError(400, "enc_required", "Секретные чаты принимают только зашифрованные сообщения")
    if kind in ("group", "channel") and ch["public"] and enc:
        raise ApiError(400, "no_enc_public", "Публичные чаты и чаты с ботами не шифруются")
    poll = p.get("poll") if isinstance(p.get("poll"), dict) else None
    media_id = int(p.get("media_id") or 0)
    if not body and not media_id and not poll:
        raise ApiError(400, "bad_body", "Пустое сообщение")
    if len(body) > 60000:
        raise ApiError(400, "bad_body", "Слишком длинное сообщение")
    if not enc and media_id and len(body) > lim(app, sender, "caption"):
        raise ApiError(400, "caption_too_long", "Подпись длиннее %d символов" % lim(app, sender, "caption"))
    if media_id and not ext_media.media_ok(app, sender, media_id):
        raise ApiError(400, "bad_media", "Файл не найден или недоступен")
    # slow mode / flood
    if ch["slowmode"] and role == "member":
        last = db.one("select max(ts) t from messages where chat_id=? and sender_id=?", (ch["id"], sender["id"]))["t"] or 0
        if now() - last < ch["slowmode"]:
            raise ApiError(429, "slowmode", "Медленный режим: подождите %d с" % (ch["slowmode"] - (now() - last)))
    if not sender["is_bot"]:
        rate(app, ("msg", sender["id"]), 20, 10)
    # direct chats: blocks, privacy, paid messages
    price, peer = 0, None
    if kind in ("private", "bot", "secret"):
        peer = db.one("select u.* from members mm join users u on u.id=mm.user_id where mm.chat_id=? and mm.user_id!=?", (ch["id"], sender["id"]))
        if peer:
            if is_blocked(app, peer["id"], sender["id"]):
                raise ApiError(403, "blocked", "Пользователь ограничил вам доступ")
            if not peer["is_bot"] and not sender["is_bot"]:
                if not can(app, peer, sender["id"], "messages"):
                    raise ApiError(403, "privacy", "Пользователь принимает сообщения только от контактов")
                if mkind in ("voice", "round") and not can(app, peer, sender["id"], "voice"):
                    raise ApiError(403, "privacy", "Пользователь запретил голосовые и видеосообщения")
                price = privacy_of(app, peer["id"])["paid_messages_stars"]
                if price and is_contact(app, peer["id"], sender["id"]):
                    price = 0
    # forward / reply / markup / poll / paid / ttl
    fwd = p.get("fwd") if isinstance(p.get("fwd"), dict) else None
    if fwd:
        src = db.one("select * from chats where id=?", (int(fwd.get("chat_id") or 0),))
        if src and src["noforwards"]:
            raise ApiError(403, "noforwards", "Автор чата запретил пересылку и копирование")
        fa = db.one("select * from users where id=?", (int(fwd.get("from_id") or 0),))
        hide = bool(fa) and not can(app, fa, sender["id"], "forwards")
        fwd = {"from_name": s(fwd.get("from_name"), 64), "from_id": 0 if hide else int(fwd.get("from_id") or 0), "hidden": hide, "chat_id": int(fwd.get("chat_id") or 0) if (src and src["public"]) else 0,
               "msg_id": int(fwd.get("msg_id") or 0), "date": int(fwd.get("date") or 0)}
    reply_to = int(p.get("reply_to") or 0)
    if reply_to and not db.one("select 1 from messages where id=? and chat_id=?", (reply_to, ch["id"])):
        raise ApiError(400, "bad_reply", "Сообщение для ответа не найдено в этом чате")
    markup = p.get("markup") if isinstance(p.get("markup"), dict) and (sender["is_bot"] or p.get("via_bot")) else None
    if markup and len(json.dumps(markup)) > 8000:
        raise ApiError(400, "bad_markup", "Слишком большая клавиатура")
    meta = p.get("meta") if isinstance(p.get("meta"), dict) else {}
    if len(json.dumps(meta)) > 16000:
        raise ApiError(400, "bad_meta", "Слишком большие метаданные")
    via_bot = 0
    if p.get("via_bot"):
        vb = user_by_login(app, p["via_bot"]) if isinstance(p["via_bot"], str) else db.one("select * from users where id=?", (int(p["via_bot"]),))
        via_bot = vb["id"] if vb and vb["is_bot"] else 0
    if mkind == "sticker" and meta.get("sticker_id"):
        sp = db.one("select p.premium from stickers s join sticker_packs p on p.id=s.pack_id where s.id=?", (int(meta["sticker_id"]),))
        if sp and sp["premium"] and not is_premium(sender):
            raise ApiError(402, "premium_required", "Этот стикер доступен только с подпиской Premium")
    paid_stars = int(p.get("paid_stars") or 0)
    if paid_stars:
        if kind != "channel" or not ch["public"]:
            raise ApiError(400, "bad_paid", "Платные посты возможны только в публичных каналах")
    if poll:
        if not ch["public"]:
            raise ApiError(400, "poll_private", "В приватных чатах опрос отправляется зашифрованным сообщением (kind=poll), подсчёт ведут клиенты")
        q, opts = s(poll.get("question"), 300), [s(o, 100) for o in (poll.get("options") or []) if s(o, 100)]
        if not q or not 2 <= len(opts) <= 10:
            raise ApiError(400, "bad_poll", "Опрос: вопрос и от 2 до 10 вариантов")
        mkind, body = "poll", body or q
        correct = int(poll.get("correct", -1)) if poll.get("quiz") else -1
        if poll.get("quiz") and not 0 <= correct < len(opts):
            raise ApiError(400, "bad_poll", "Для викторины укажите номер правильного ответа")
    ttl = max(0, min(int(p.get("ttl") or 0), 604800))
    expires = now() + ch["ttl"] if ch["ttl"] else 0
    target = ""
    if kind == "secret":
        om = db.one("select device_id from members where chat_id=? and user_id!=?", (ch["id"], sender["id"])) or db.one("select device_id from members where chat_id=? and user_id=? and device_id!=?", (ch["id"], sender["id"], device))
        target = om["device_id"] if om else ""
    # --- charge for paid direct messages
    if price:
        import ext_stars
        ext_stars.add_stars(app, sender["id"], -price, "paid_message", str(ch["id"]), "Платное сообщение", need=True)
        ext_stars.add_stars(app, peer["id"], price, "paid_message_in", str(ch["id"]), "Платное сообщение от @" + sender["username"])
    mid = db.x("insert into messages(chat_id,sender_id,ts,body,enc,reply_to,media_id,kind,meta,fwd,markup,paid_stars,expires_at,ttl,target_device,via_bot,group_id,from_device) "
               "values(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
               (ch["id"], sender["id"], now(), body, enc, reply_to, media_id, mkind, json.dumps(meta) if meta else "", json.dumps(fwd) if fwd else "", json.dumps(markup) if markup else "",
                paid_stars, expires, ttl, target, via_bot, int(p.get("group_id") or 0), device))
    if poll:
        db.x("insert into polls(msg_id,question,options,multiple,anonymous,quiz,correct,explanation,closes_at) values(?,?,?,?,?,?,?,?,?)",
             (mid, q, json.dumps(opts), int(bool(poll.get("multiple"))), int(poll.get("anonymous", True)), int(bool(poll.get("quiz"))), correct, s(poll.get("explanation"), 200),
              int(poll.get("closes_at") or 0)))
    db.x("insert into read_state(chat_id,user_id,last_read) values(?,?,?) on conflict(chat_id,user_id) do update set last_read=max(last_read,excluded.last_read)", (ch["id"], sender["id"], mid))
    notify()
    try:
        import ext_bots
        ext_bots.on_message(app, ch, db.one(SEL + " where m.id=?", (mid,)), sender)
    except Exception:
        log.exception("bot dispatch failed")
    return mid


def sched_limit(app, chat_id, uid):
    return app.db.one("select count(*) n from scheduled where chat_id=? and sender_id=?", (chat_id, uid))["n"]


@route("POST", "/api/messages/send")
def messages_send(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if not m:
        raise ApiError(403, "forbidden", "Вы не участник этого чата")
    at = int(c.body.get("schedule_at") or 0)
    dev = c.s["device_id"]
    if at > now() + 10:
        if at > now() + 365 * 86400 or sched_limit(app, ch["id"], c.u["id"]) >= 100:
            raise ApiError(400, "bad_schedule", "Нельзя запланировать так далеко или больше 100 сообщений")
        sid = app.db.x("insert into scheduled(chat_id,sender_id,send_at,payload) values(?,?,?,?)", (ch["id"], c.u["id"], at, json.dumps(dict(c.body, _device=dev))))
        return {"scheduled": sid, "send_at": at}
    mid = post_message(app, c.u, ch, m, c.body, dev)
    return {"id": mid, "ts": now()}


@route("GET", "/api/messages/scheduled")
def scheduled_list(app, c):
    cid = int(c.q.get("chat_id") or 0)
    return {"scheduled": [{"id": r["id"], "send_at": r["send_at"], "payload": json.loads(r["payload"])} for r in
                          app.db.q("select * from scheduled where chat_id=? and sender_id=? order by send_at", (cid, c.u["id"]))]}


@route("POST", "/api/messages/scheduled/delete")
def scheduled_delete(app, c):
    app.db.x("delete from scheduled where id=? and sender_id=?", (int(c.body.get("id") or 0), c.u["id"]))
    return {"ok": True}


@route("POST", "/api/messages/scheduled/send_now")
def scheduled_now(app, c):
    r = app.db.one("select * from scheduled where id=? and sender_id=?", (int(c.body.get("id") or 0), c.u["id"]))
    if not r:
        raise ApiError(404, "no_scheduled", "Запланированное сообщение не найдено")
    app.db.x("update scheduled set send_at=? where id=?", (now(), r["id"]))
    return {"ok": True}


def deliver_scheduled(app):
    for r in app.db.q("select * from scheduled where send_at<=? limit 100", (now(),)):
        app.db.x("delete from scheduled where id=?", (r["id"],))
        try:
            ch, m = get_member(app, r["chat_id"], r["sender_id"])
            u = app.db.one("select * from users where id=?", (r["sender_id"],))
            if m and u and not u["banned"]:
                p = json.loads(r["payload"])
                post_message(app, u, ch, m, p, p.get("_device", ""))
        except Exception as e:
            log.warning("scheduled %s failed: %s", r["id"], e)


# ------------------------------------------------------------------ edit / delete / react / pin / read / typing
def own_message(app, c, mid=None):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if not m:
        raise ApiError(403, "forbidden", "Вы не участник этого чата")
    msg = app.db.one("select * from messages where id=? and chat_id=? and deleted=0", (int(mid or c.body.get("msg_id") or 0), ch["id"]))
    if not msg:
        raise ApiError(404, "no_message", "Сообщение не найдено")
    return ch, m, msg


@route("POST", "/api/messages/edit")
def messages_edit(app, c):
    ch, m, msg = own_message(app, c)
    if msg["sender_id"] != c.u["id"]:
        raise ApiError(403, "forbidden", "Редактировать можно только свои сообщения")
    if now() - msg["ts"] > int(app.cfg.get("limits", "edit_window_hours", 48)) * 3600:
        raise ApiError(403, "too_old", "Сообщение слишком старое для редактирования")
    if msg["kind"] in ("poll", "invoice", "service"):
        raise ApiError(400, "not_editable", "Это сообщение нельзя редактировать")
    body, enc = c.body.get("body"), 1 if c.body.get("enc") else 0
    if not isinstance(body, str) or (not body and not msg["media_id"]) or len(body) > 60000 or enc != msg["enc"]:
        raise ApiError(400, "bad_body", "Некорректный текст (шифрование должно совпадать с исходным сообщением)")
    markup = c.body.get("markup") if isinstance(c.body.get("markup"), dict) and (c.u["is_bot"] or msg["via_bot"]) else None
    app.db.x("update messages set body=?, edited_at=?, markup=coalesce(?, markup) where id=?", (body, now(), json.dumps(markup) if markup else None, msg["id"]))
    emit(app, ch["id"], "edit", {"msg_id": msg["id"], "body": body, "enc": enc, "edited_at": now(), "markup": markup})
    try:
        import ext_bots
        ext_bots.on_edit(app, ch, app.db.one(SEL + " where m.id=?", (msg["id"],)), c.u)
    except Exception:
        log.exception("bot edit dispatch failed")
    return {"ok": True}


def wipe(app, ch, mids):
    for mid in mids:
        app.db.x("update messages set deleted=1, body='', media_id=0, meta='', markup='' where id=?", (mid,))
        for t in ("reactions", "poll_votes", "polls", "message_views"):
            app.db.x("delete from %s where msg_id=?" % t, (mid,))
    if mids:
        emit(app, ch["id"], "delete", {"msg_ids": list(mids)})


@route("POST", "/api/messages/delete")
def messages_delete(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if not m:
        raise ApiError(403, "forbidden", "Вы не участник этого чата")
    ids = [int(x) for x in (c.body.get("msg_ids") or [])][:200]
    revoke = c.body.get("revoke", True)
    todo = []
    for mid in ids:
        msg = app.db.one("select * from messages where id=? and chat_id=? and deleted=0", (mid, ch["id"]))
        if not msg:
            continue
        mine = msg["sender_id"] == c.u["id"]
        if revoke and (mine or ch["kind"] in ("private", "bot", "secret") or m["role"] in ("owner", "admin", "moderator")):
            todo.append(mid)
        elif not revoke:
            app.db.x("insert or ignore into hides(user_id,msg_id) values(?,?)", (c.u["id"], mid))
        else:
            raise ApiError(403, "forbidden", "Нельзя удалить чужое сообщение для всех")
    wipe(app, ch, todo)
    return {"ok": True, "deleted": todo}


@route("POST", "/api/messages/react")
def messages_react(app, c):
    ch, m, msg = own_message(app, c)
    em = c.body.get("emoji")
    em = [] if not em else ([em] if isinstance(em, str) else list(em))
    em = [s(e, 16) for e in em if s(e, 16)]
    if len(set(em)) > lim(app, c.u, "reactions"):
        raise ApiError(402 if not is_premium(c.u) else 400, "reactions_limit", "Можно поставить реакций: %d" % lim(app, c.u, "reactions"))
    app.db.x("delete from reactions where msg_id=? and user_id=?", (msg["id"], c.u["id"]))
    for e in dict.fromkeys(em):
        app.db.x("insert into reactions(msg_id,user_id,emoji,ts) values(?,?,?,?)", (msg["id"], c.u["id"], e, now()))
    emit(app, ch["id"], "reaction", {"msg_id": msg["id"], "counts": [{"emoji": r["emoji"], "count": r["count"]} for r in reaction_summary(app, msg["id"], 0)],
                                     "actor": c.u["id"], "actor_emojis": em})
    return {"ok": True}


@route("POST", "/api/messages/pin")
def messages_pin(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if not m or (ch["kind"] in ("group", "channel") and m["role"] not in ("owner", "admin")):
        raise ApiError(403, "forbidden", "Недостаточно прав")
    mid = int(c.body.get("msg_id") or 0)
    if mid and not app.db.one("select 1 from messages where id=? and chat_id=? and deleted=0", (mid, ch["id"])):
        raise ApiError(404, "no_message", "Сообщение не найдено")
    app.db.x("update chats set pinned_msg=? where id=?", (mid, ch["id"]))
    emit(app, ch["id"], "pin", {"msg_id": mid, "by": c.u["id"]})
    return {"ok": True}


@route("POST", "/api/messages/read")
def messages_read(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if not m:
        raise ApiError(403, "forbidden", "Вы не участник этого чата")
    mx = int(c.body.get("max_id") or 0)
    prev = app.db.one("select last_read from read_state where chat_id=? and user_id=?", (ch["id"], c.u["id"]))
    prev = prev["last_read"] if prev else 0
    if mx <= prev:
        return {"ok": True}
    app.db.x("insert into read_state(chat_id,user_id,last_read) values(?,?,?) on conflict(chat_id,user_id) do update set last_read=excluded.last_read", (ch["id"], c.u["id"], mx))
    if ch["kind"] == "channel":      # post views: once per user
        for r in app.db.q("select id from messages where chat_id=? and id>? and id<=? and deleted=0 limit 500", (ch["id"], prev, mx)):
            if app.db.x("insert or ignore into message_views(msg_id,user_id) values(?,?)", (r["id"], c.u["id"])) is not None and app.db.c.execute("select changes()").fetchone()[0]:
                app.db.x("update messages set views=views+1 where id=?", (r["id"],))
    # self-destruct: the timer starts when the recipient reads
    for r in app.db.q("select id from messages where chat_id=? and id>? and id<=? and ttl>0 and expires_at=0 and sender_id!=? and deleted=0", (ch["id"], prev, mx, c.u["id"])):
        app.db.x("update messages set expires_at=? where id=?", (now() + app.db.one("select ttl from messages where id=?", (r["id"],))["ttl"], r["id"]))
    if ch["kind"] in ("private", "bot") and privacy_of(app, c.u["id"])["read_receipts"]["rule"] != "nobody":
        emit(app, ch["id"], "read", {"user_id": c.u["id"], "max_id": mx})
    return {"ok": True}


@route("POST", "/api/typing")
def typing(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    act = s(c.body.get("action"), 20) or "typing"
    if not m or act not in ACTIONS or ch["kind"] == "channel":
        return {"ok": True}
    for r in app.db.q("select user_id from members where chat_id=? and user_id!=? limit 100", (ch["id"], c.u["id"])):
        if ch["kind"] != "secret" or True:
            push_eph(app, r["user_id"], {"type": "typing", "chat_id": ch["id"], "user_id": c.u["id"], "username": c.u["username"], "action": act, "ts": now()})
    return {"ok": True}


# ------------------------------------------------------------------ polls
@route("POST", "/api/polls/vote")
def poll_vote(app, c):
    ch, m, msg = own_message(app, c)
    p = app.db.one("select * from polls where msg_id=?", (msg["id"],))
    if not p or p["closed"] or (p["closes_at"] and p["closes_at"] <= now()):
        raise ApiError(400, "poll_closed", "Опрос закрыт или не найден")
    opts = [int(x) for x in (c.body.get("options") or [])]
    n = len(json.loads(p["options"]))
    if any(not 0 <= o < n for o in opts) or len(set(opts)) != len(opts) or (len(opts) > 1 and not p["multiple"]):
        raise ApiError(400, "bad_vote", "Недопустимый выбор")
    had = app.db.one("select 1 from poll_votes where msg_id=? and user_id=?", (msg["id"], c.u["id"]))
    if had and p["quiz"]:
        raise ApiError(400, "quiz_final", "В викторине ответ нельзя изменить")
    app.db.x("delete from poll_votes where msg_id=? and user_id=?", (msg["id"], c.u["id"]))
    for o in opts:
        app.db.x("insert into poll_votes(msg_id,user_id,opt) values(?,?,?)", (msg["id"], c.u["id"], o))
    emit(app, ch["id"], "poll", {"msg_id": msg["id"], "poll": poll_dict(app, msg["id"], 0), "voter": None if p["anonymous"] else c.u["id"]})
    return {"poll": poll_dict(app, msg["id"], c.u["id"])}


@route("GET", "/api/polls/voters")
def poll_voters(app, c):
    ch, m = get_member(app, int(c.q.get("chat_id") or 0), c.u["id"])
    p = app.db.one("select * from polls where msg_id=?", (int(c.q.get("msg_id") or 0),))
    if not (m or ch["public"]) or not p or p["anonymous"]:
        raise ApiError(403, "forbidden", "Голоса анонимны или нет доступа")
    return {"voters": [{"user_id": r["user_id"], "username": r["username"], "option": r["opt"]} for r in
                       app.db.q("select v.user_id,v.opt,u.username from poll_votes v join users u on u.id=v.user_id where v.msg_id=? limit 500", (p["msg_id"],))]}


@route("POST", "/api/polls/close")
def poll_close(app, c):
    ch, m, msg = own_message(app, c)
    if msg["sender_id"] != c.u["id"] and m["role"] not in ("owner", "admin"):
        raise ApiError(403, "forbidden", "Недостаточно прав")
    app.db.x("update polls set closed=1 where msg_id=?", (msg["id"],))
    emit(app, ch["id"], "poll", {"msg_id": msg["id"], "poll": poll_dict(app, msg["id"], 0)})
    return {"ok": True}


# ------------------------------------------------------------------ direct chats
@route("POST", "/api/dm/open")
def dm_open(app, c):
    peer = user_by_login(app, c.body.get("login"))
    if peer["id"] == c.u["id"]:
        raise ApiError(400, "self", "Нельзя написать самому себе (используйте «Избранное»)")
    if peer["banned"]:
        raise ApiError(403, "banned", "Пользователь заблокирован")
    if is_blocked(app, peer["id"], c.u["id"]):
        raise ApiError(403, "blocked", "Пользователь ограничил вам доступ")
    # Peer public key is optional: E2E starts after both clients publish keys.
    if not peer["is_bot"] and not peer["pub"]:
        pass
    key = "%d:%d" % (min(c.u["id"], peer["id"]), max(c.u["id"], peer["id"]))
    ch = app.db.one("select * from chats where dm_key=?", (key,))
    if not ch:
        cid = app.db.x("insert into chats(kind,dm_key,owner_id,created) values(?,?,?,?)", ("bot" if peer["is_bot"] else "private", key, c.u["id"], now()))
        for uid in (c.u["id"], peer["id"]):
            app.db.x("insert into members(chat_id,user_id,joined) values(?,?,?)", (cid, uid, now()))
        ch = app.db.one("select * from chats where id=?", (cid,))
        if peer["is_bot"]:
            import ext_bots
            ext_bots.on_started(app, ch, c.u, peer)
    return {"chat": chat_dict(app, ch, c.u["id"], history=True)}


# ------------------------------------------------------------------ history, search
@route("GET", "/api/messages/history")
def history(app, c):
    ch, m = get_member(app, int(c.q.get("chat_id") or 0), c.u["id"])
    if not (m or ch["public"]):
        raise ApiError(403, "forbidden", "Нет доступа")
    before, limit = int(c.q.get("before") or 0) or 2 ** 62, max(1, min(int(c.q.get("limit") or 50), 100))
    floor = m["since_msg"] if m else 0
    rows = app.db.q(SEL + " where m.chat_id=? and m.id<? and m.id>? and m.deleted=0 and m.target_device=''" + VISIBLE + " order by m.id desc limit ?", (ch["id"], before, floor, c.u["id"], limit))
    return {"messages": [full_msg(app, r, c.u["id"]) for r in reversed(rows)]}


@route("GET", "/api/messages/get")
def messages_get(app, c):
    ch, m = get_member(app, int(c.q.get("chat_id") or 0), c.u["id"])
    if not (m or ch["public"]):
        raise ApiError(403, "forbidden", "Нет доступа")
    ids = [int(x) for x in c.q.get("ids", "").split(",") if x.strip().isdigit()][:100]
    rows = [app.db.one(SEL + " where m.id=? and m.chat_id=? and m.deleted=0", (i, ch["id"])) for i in ids]
    return {"messages": [full_msg(app, r, c.u["id"]) for r in rows if r]}


@route("GET", "/api/messages/search")
def messages_search(app, c):
    q = s(c.q.get("q"), 60)
    if len(q) < 2:
        return {"messages": []}
    cid, like = int(c.q.get("chat_id") or 0), "%" + q.replace("%", "") + "%"
    if cid:
        ch, m = get_member(app, cid, c.u["id"])
        if not (m or ch["public"]):
            raise ApiError(403, "forbidden", "Нет доступа")
        if ch["kind"] in ("private", "secret") or (not ch["public"] and ch["kind"] != "bot"):
            raise ApiError(400, "encrypted", "Это чат со сквозным шифрованием: сервер не видит текст, ищите на устройстве")
        rows = app.db.q(SEL + " where m.chat_id=? and m.deleted=0 and m.enc=0 and m.body like ? order by m.id desc limit 50", (cid, like))
    else:   # global: public chats + chats with bots the user belongs to (plaintext only)
        rows = app.db.q(SEL + " join chats c on c.id=m.chat_id left join members mm on mm.chat_id=m.chat_id and mm.user_id=? where m.deleted=0 and m.enc=0 and m.body like ? and "
                        "(c.public=1 or mm.user_id is not null) and c.banned=0 order by m.id desc limit 50", (c.u["id"], like))
    return {"messages": [full_msg(app, r, c.u["id"]) for r in rows]}


# ------------------------------------------------------------------ sync (long poll)
def chat_extras(app, ch, uid, d):
    rs = app.db.one("select last_read from read_state where chat_id=? and user_id=?", (ch["id"], uid))
    last = rs["last_read"] if rs else 0
    mem = app.db.one("select since_msg from members where chat_id=? and user_id=?", (ch["id"], uid))
    d["last_read"] = last
    d["unread"] = app.db.one("select count(*) n from messages where chat_id=? and id>? and id>? and sender_id!=? and deleted=0 and target_device=''", (ch["id"], last, mem["since_msg"] if mem else 0, uid))["n"] if mem else 0
    if ch["kind"] in ("private", "bot"):
        o = app.db.one("select mm.user_id, r.last_read from members mm left join read_state r on r.chat_id=mm.chat_id and r.user_id=mm.user_id where mm.chat_id=? and mm.user_id!=?", (ch["id"], uid))
        if o and privacy_of(app, uid)["read_receipts"]["rule"] != "nobody" and privacy_of(app, o["user_id"])["read_receipts"]["rule"] != "nobody":
            d["peer_read"] = o["last_read"] or 0
    p = app.db.one("select * from chat_prefs where user_id=? and chat_id=?", (uid, ch["id"]))
    if p:
        d["prefs"] = {"pinned": bool(p["pinned"]), "muted_until": p["muted_until"], "archived": bool(p["archived"]), "data": json.loads(p["data"])}
    dr = app.db.one("select * from drafts where user_id=? and chat_id=?", (uid, ch["id"]))
    if dr:
        d["draft"] = {"text": dr["text"], "reply_to": dr["reply_to"]}
    gc = app.db.one("select id from calls where chat_id=? and grp=1 and state='active' order by id desc limit 1", (ch["id"],))
    if gc:
        d["active_call"] = gc["id"]


EXTRA_CHAT.append(chat_extras)


@route("GET", "/api/sync")
def sync(app, c):
    q = c.q
    since = int(q.get("since") or 0)
    ev_param = q.get("ev")
    wait = min(int(q.get("wait") or 0), int(app.cfg.get("server", "sync_wait_seconds", 20)))
    known = {int(x) for x in q.get("known", "").split(",") if x.strip().isdigit()}
    uid, db, dev = c.u["id"], app.db, c.s["device_id"]
    deadline = time.time() + wait
    ev_since = int(ev_param) if ev_param is not None else None
    if ev_since is None:
        ev_since = db.one("select coalesce(max(id),0) n from events")["n"]
        first_events = True
    else:
        first_events = False
    while True:
        msgs = db.q(SEL + " join members mem on mem.chat_id=m.chat_id and mem.user_id=? where m.id>? and m.id>mem.since_msg and m.deleted=0 and (m.target_device='' or m.target_device=?)"
                    " and (mem.device_id='' or mem.device_id=?)" + VISIBLE + " order by m.id limit 500", (uid, since, dev, dev, uid))
        newc = []
        for ch in db.q("select c.* from chats c join members mm on mm.chat_id=c.id where mm.user_id=? and (mm.device_id='' or mm.device_id=?)", (uid, dev)):
            if ch["id"] in known:
                continue
            if ch["kind"] == "private" and not db.one("select 1 from messages where chat_id=? limit 1", (ch["id"],)):
                continue
            newc.append(ch)
        evs = [] if first_events else db.q("select e.*, mem.role from events e join members mem on mem.chat_id=e.chat_id and mem.user_id=? where e.id>? order by e.id limit 500", (uid, ev_since))
        evs = [e for e in evs if e["kind"] != "join_request" or e["role"] in ("owner", "admin")]
        eph = drain_eph(app, uid)
        if msgs or newc or evs or eph or time.time() >= deadline:
            break
        with NEWS:
            NEWS.wait(timeout=min(2.0, max(0.1, deadline - time.time())))
    last = max([since] + [r["id"] for r in msgs])
    last_ev = max([ev_since] + [e["id"] for e in evs])
    fresh = db.one("select * from users where id=?", (uid,))
    import api
    return {"messages": [full_msg(app, r, uid) for r in msgs], "chats": [chat_dict(app, ch, uid, history=True) for ch in newc],
            "events": [{"id": e["id"], "chat_id": e["chat_id"], "kind": e["kind"], "data": json.loads(e["data"]), "ts": e["ts"]} for e in evs],
            "ephemeral": eph, "last": last, "last_event": last_ev, "me": api.me_dict(fresh)}


@route("GET", "/api/status")
def status(app, c):
    """Online / last-seen status of users (privacy rules applied): ?ids=1,2,3"""
    import ext_profile
    out = {}
    for i in [int(x) for x in c.q.get("ids", "").split(",") if x.strip().isdigit()][:100]:
        t = app.db.one("select * from users where id=? and deleted=0", (i,))
        if t:
            out[str(i)] = ext_profile.status_view(app, c.u, t)
    return {"status": out}
