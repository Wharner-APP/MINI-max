"""Profile, avatar, privacy / notification / folder settings, contacts, blocks, 2FA, sessions, drafts, chat preferences,
invite links, join requests, chat administration."""
import base64, hashlib, hmac, json, re, secrets, struct, time

from api import (ApiError, UNAME_RE, can, chat_dict, emit, get_member, get_setting, hash_key, is_blocked, is_contact, is_premium, lim, me_dict, name_taken,
                 notify, now, privacy_of, pub_user, route, s, user_by_login, PRIVACY_KEYS, SETTING_KINDS, rate)


# ------------------------------------------------------------------ TOTP (RFC 6238)
def totp_code(secret_b32, t=None):
    key = base64.b32decode(secret_b32)
    n = int((t or time.time()) // 30)
    h = hmac.new(key, struct.pack(">Q", n), "sha1").digest()
    o = h[-1] & 15
    return "%06d" % ((struct.unpack(">I", h[o:o + 4])[0] & 0x7FFFFFFF) % 1000000)


def totp_ok(secret_b32, code):
    t = time.time()
    return bool(code) and any(hmac.compare_digest(totp_code(secret_b32, t + d * 30), code) for d in (-1, 0, 1))


def need_admin(app, chat_id, uid, owner_only=False):
    ch, m = get_member(app, chat_id, uid)
    if not m or m["role"] not in (("owner",) if owner_only else ("owner", "admin")):
        raise ApiError(403, "forbidden", "Недостаточно прав")
    return ch, m


def status_view(app, viewer, t):
    if t["is_bot"]:
        return {"status": "bot"}
    ls, n = t["last_seen"], now()
    own_hidden = privacy_of(app, viewer["id"])["last_seen"]["rule"] == "nobody" and not is_premium(viewer)
    if can(app, t, viewer["id"], "last_seen") and not own_hidden:
        return {"status": "online" if n - ls < 65 else "offline", "last_seen": ls}
    age = n - ls
    return {"status": "recently" if age < 3 * 86400 else "within_week" if age < 7 * 86400 else "within_month" if age < 30 * 86400 else "long_ago"}


# ------------------------------------------------------------------ profile
@route("POST", "/api/profile/update")
def profile_update(app, c):
    b, u, db = c.body, c.u, app.db
    if "display_name" in b:
        nm = s(b["display_name"], 40)
        if not nm:
            raise ApiError(400, "bad_name", "Имя не может быть пустым")
        db.x("update users set name=? where id=?", (nm, u["id"]))
    if "bio" in b:
        if len(str(b["bio"])) > lim(app, u, "bio"):
            raise ApiError(400, "bio_too_long", "Слишком длинное описание (лимит %d символов)" % lim(app, u, "bio"))
        db.x("update users set bio=? where id=?", (s(b["bio"], 500), u["id"]))
    if "username" in b:
        un = s(b["username"], 40).lstrip("@")
        if un.lower() != (u["username"] or "").lower():
            if not UNAME_RE.fullmatch(un) and not (len(un) >= 3 and re.fullmatch(r"[A-Za-z0-9_.]{3,32}", un) and is_premium(u)):
                raise ApiError(400, "bad_username", "Имя пользователя: 5–32 символа, латиница, цифры и «_», начинается с буквы")
            if name_taken(app, un, u["id"]):
                raise ApiError(409, "username_taken", "Это имя пользователя уже занято")
            rate(app, ("uname", u["id"]), 5, 3600)
            db.x("update users set username=? where id=?", (un, u["id"]))
    if "birthday" in b:
        bd = s(b["birthday"], 10)
        if bd and not re.fullmatch(r"(\d{4}-)?\d{2}-\d{2}", bd):
            raise ApiError(400, "bad_birthday", "Формат даты: ГГГГ-ММ-ДД или ММ-ДД")
        db.x("update users set birthday=? where id=?", (bd, u["id"]))
    for field, col in (("emoji_status", "emoji_status"), ("name_color", "name_color")):
        if field in b:
            if not is_premium(u) and b[field] not in ("", 0, None):
                raise ApiError(402, "premium_required", "Эта функция доступна с подпиской Premium")
            db.x("update users set %s=? where id=?" % col, (s(b[field], 32) if field == "emoji_status" else int(b[field] or 0) % 8, u["id"]))
    return {"user": pub_user(db.one("select * from users where id=?", (u["id"],)))}


@route("POST", "/api/profile/avatar")
def profile_avatar(app, c):
    mid = int(c.body.get("media_id") or 0)
    if mid:
        m = app.db.one("select * from media where id=? and owner_id=? and ready=1", (mid, c.u["id"]))
        if not m or m["kind"] not in ("avatar", "photo", "video"):
            raise ApiError(400, "bad_media", "Нужно загруженное фото или видео-аватар")
    app.db.x("update users set avatar_id=? where id=?", (mid, c.u["id"]))
    return {"avatar_id": mid}


@route("GET", "/api/profile/get")
def profile_get(app, c):
    t = app.db.one("select * from users where id=? and deleted=0", (int(c.q["id"]),)) if c.q.get("id") else user_by_login(app, c.q.get("login"))
    if not t:
        raise ApiError(404, "no_user", "Пользователь не найден")
    v = c.u
    d = {"id": t["id"], "username": t["username"] or t["login"], "display_name": t["name"], "is_bot": bool(t["is_bot"]), "premium": is_premium(t),
         "emoji_status": t["emoji_status"], "name_color": t["name_color"], "is_contact": is_contact(app, v["id"], t["id"]), "blocked": is_blocked(app, v["id"], t["id"]),
         "blocked_me": is_blocked(app, t["id"], v["id"]), "business": t["business_until"] > now()}
    d.update(status_view(app, v, t))
    d["bio"] = t["bio"] if can(app, t, v["id"], "bio") else ""
    d["avatar_id"] = t["avatar_id"] if can(app, t, v["id"], "photos") else 0
    d["birthday"] = t["birthday"] if can(app, t, v["id"], "birthday") else ""
    d["can_write"] = can(app, t, v["id"], "messages") and not is_blocked(app, t["id"], v["id"])
    d["can_call"] = can(app, t, v["id"], "calls") and not is_blocked(app, t["id"], v["id"])
    d["pub"] = t["pub"]
    if can(app, t, v["id"], "gifts"):
        d["gifts"] = [{"id": g["id"], "gift_id": g["gift_id"], "serial": g["serial"], "stars": g["stars"], "ts": g["ts"], "note": g["note"],
                       "from": None if g["anonymous"] else g["from_id"]} for g in app.db.q("select * from gifts where user_id=? and hidden=0 and converted=0 order by pinned desc,id desc limit 60", (t["id"],))]
    if t["is_bot"]:
        d["bot"] = {"about": t["bot_about"], "webapp_url": t["webapp_url"], "inline": bool(t["bot_inline"]),
                    "commands": [{"command": r["command"], "description": r["description"]} for r in app.db.q("select * from bot_commands where bot_id=? order by rowid", (t["id"],))]}
    d["common_chats"] = app.db.one("select count(*) n from members a join members b on a.chat_id=b.chat_id join chats c on c.id=a.chat_id "
                                   "where a.user_id=? and b.user_id=? and c.kind in ('group','channel')", (v["id"], t["id"]))["n"]
    return d


# ------------------------------------------------------------------ settings (privacy / notifications / folders / ui ...)
def norm_privacy(app, u, data):
    out = dict(data)
    for k in PRIVACY_KEYS:
        if k in data:
            v = data[k]
            rule = v.get("rule") if isinstance(v, dict) else v
            if rule not in ("all", "contacts", "nobody"):
                raise ApiError(400, "bad_privacy", "Недопустимое значение для «%s»" % k)
            if isinstance(v, dict):
                v = {"rule": rule, "allow": [int(x) for x in v.get("allow", [])][:200], "deny": [int(x) for x in v.get("deny", [])][:200]}
            if k in ("messages", "voice") and rule != "all" and not is_premium(u):
                raise ApiError(402, "premium_required", "Ограничение входящих сообщений доступно с подпиской Premium")
            out[k] = v
    if int(data.get("paid_messages_stars", 0) or 0) and not is_premium(u):
        raise ApiError(402, "premium_required", "Платные сообщения доступны с подпиской Premium")
    for k, lo, hi in (("delete_after_months", 1, 24), ("session_ttl_days", 7, 365), ("paid_messages_stars", 0, 10000)):
        if k in data:
            out[k] = max(lo, min(hi, int(data[k] or 0)))
    return out


@route("GET", "/api/settings")
def settings_get(app, c):
    kind = s(c.q.get("kind"), 20)
    if kind:
        if kind not in SETTING_KINDS:
            raise ApiError(400, "bad_kind", "Неизвестный раздел настроек")
        return {"kind": kind, "data": privacy_of(app, c.u["id"]) if kind == "privacy" else get_setting(app, c.u["id"], kind, {})}
    out = {k: get_setting(app, c.u["id"], k, {}) for k in SETTING_KINDS}
    out["privacy"] = privacy_of(app, c.u["id"])
    return {"settings": out}


@route("POST", "/api/settings")
def settings_set(app, c):
    kind, data = s(c.body.get("kind"), 20), c.body.get("data")
    if kind not in SETTING_KINDS or not isinstance(data, (dict, list)):
        raise ApiError(400, "bad_request", "Укажите раздел и данные")
    raw = json.dumps(data, ensure_ascii=False)
    if len(raw) > 65536:
        raise ApiError(413, "too_large", "Слишком большие настройки")
    if kind == "privacy":
        cur = dict(get_setting(app, c.u["id"], "privacy", {}) or {})
        cur.update(norm_privacy(app, c.u, data if isinstance(data, dict) else {}))
        raw = json.dumps(cur)
    elif kind == "folders":
        if not isinstance(data, list) or len(data) > lim(app, c.u, "folders"):
            raise ApiError(402 if is_premium(c.u) is False else 400, "folders_limit", "Лимит папок: %d" % lim(app, c.u, "folders"))
        for f in data:
            if not isinstance(f, dict) or not s(f.get("name"), 12):
                raise ApiError(400, "bad_folder", "У папки должно быть название (до 12 символов)")
    app.db.x("insert or replace into user_settings(user_id,kind,data,ts) values(?,?,?,?)", (c.u["id"], kind, raw, now()))
    return {"ok": True}


# ------------------------------------------------------------------ contacts / blocks
@route("POST", "/api/contacts/add")
def contacts_add(app, c):
    t = user_by_login(app, c.body.get("login"))
    app.db.x("insert or ignore into contacts(user_id,contact_id,ts) values(?,?,?)", (c.u["id"], t["id"], now()))
    return {"ok": True}


@route("POST", "/api/contacts/remove")
def contacts_remove(app, c):
    app.db.x("delete from contacts where user_id=? and contact_id=?", (c.u["id"], user_by_login(app, c.body.get("login"))["id"]))
    return {"ok": True}


@route("GET", "/api/contacts")
def contacts_list(app, c):
    rows = app.db.q("select u.id,u.username,u.name,u.avatar_id,u.last_seen from contacts k join users u on u.id=k.contact_id where k.user_id=? and u.deleted=0 order by u.name", (c.u["id"],))
    return {"contacts": [{"id": r["id"], "username": r["username"], "name": r["name"], "avatar_id": r["avatar_id"]} for r in rows]}


@route("POST", "/api/blocks/add")
def blocks_add(app, c):
    app.db.x("insert or ignore into blocks(user_id,blocked_id,ts) values(?,?,?)", (c.u["id"], user_by_login(app, c.body.get("login"))["id"], now()))
    return {"ok": True}


@route("POST", "/api/blocks/remove")
def blocks_remove(app, c):
    app.db.x("delete from blocks where user_id=? and blocked_id=?", (c.u["id"], user_by_login(app, c.body.get("login"))["id"]))
    return {"ok": True}


@route("GET", "/api/blocks")
def blocks_list(app, c):
    return {"blocked": [{"id": r["id"], "username": r["username"], "name": r["name"]} for r in
                        app.db.q("select u.id,u.username,u.name from blocks b join users u on u.id=b.blocked_id where b.user_id=?", (c.u["id"],))]}


# ------------------------------------------------------------------ 2FA, password, key backup, account
@route("POST", "/api/2fa/setup")
def tfa_setup(app, c):
    if c.u["totp_secret"]:
        raise ApiError(400, "already", "2FA уже включена")
    sec = base64.b32encode(secrets.token_bytes(20)).decode()
    app.pending_2fa[c.u["id"]] = sec
    return {"secret": sec, "uri": "otpauth://totp/MINI%%20max:%s?secret=%s&issuer=MINI%%20max" % (c.u["login"], sec)}


@route("POST", "/api/2fa/confirm")
def tfa_confirm(app, c):
    sec = app.pending_2fa.get(c.u["id"])
    if not sec or not totp_ok(sec, s(c.body.get("code"), 8)):
        raise ApiError(400, "bad_code", "Неверный код")
    app.db.x("update users set totp_secret=? where id=?", (sec, c.u["id"]))
    app.pending_2fa.pop(c.u["id"], None)
    return {"ok": True}


@route("POST", "/api/2fa/disable")
def tfa_disable(app, c):
    if not c.u["totp_secret"] or not totp_ok(c.u["totp_secret"], s(c.body.get("code"), 8)):
        raise ApiError(400, "bad_code", "Неверный код")
    app.db.x("update users set totp_secret='' where id=?", (c.u["id"],))
    return {"ok": True}


def check_pw(u, key):
    if not hmac.compare_digest(u["auth"], hash_key(s(key, 128), u["salt"])):
        raise ApiError(403, "bad_credentials", "Неверный пароль")


@route("POST", "/api/account/change_password")
def change_password(app, c):
    check_pw(c.u, c.body.get("old_auth_key"))
    nk = s(c.body.get("new_auth_key"), 128)
    if not re.fullmatch(r"[0-9a-f]{64}", nk):
        raise ApiError(400, "bad_key", "Некорректный ключ")
    salt = secrets.token_bytes(16)
    app.db.x("update users set auth=?, salt=?, key_backup=? where id=?", (hash_key(nk, salt), salt, s(c.body.get("key_backup"), 4000) or c.u["key_backup"], c.u["id"]))
    app.db.x("delete from sessions where user_id=? and token!=?", (c.u["id"], c.s["token"]))
    return {"ok": True}


@route("POST", "/api/account/delete")
def account_delete(app, c):
    check_pw(c.u, c.body.get("auth_key"))
    import console
    console.anonymize(app, c.u)
    return {"ok": True}


@route("GET", "/api/keys/backup")
def kb_get(app, c):
    return {"blob": c.u["key_backup"]}


@route("POST", "/api/keys/backup")
def kb_set(app, c):
    app.db.x("update users set key_backup=? where id=?", (s(c.body.get("blob"), 4000), c.u["id"]))
    return {"ok": True}


# ------------------------------------------------------------------ sessions / devices
def sid(tok):
    return hashlib.sha256(tok.encode()).hexdigest()[:12]


@route("GET", "/api/sessions")
def sessions(app, c):
    return {"sessions": [{"id": sid(r["token"]), "device": r["name"] or r["device"], "platform": r["platform"], "app": r["app"], "ip": r["ip"], "created": r["created"],
                         "last": r["last"], "current": r["token"] == c.s["token"], "device_id": r["device_id"]}
                         for r in app.db.q("select * from sessions where user_id=? order by last desc", (c.u["id"],))]}


@route("POST", "/api/sessions/terminate")
def session_terminate(app, c):
    for r in app.db.q("select token from sessions where user_id=?", (c.u["id"],)):
        if sid(r["token"]) == s(c.body.get("id"), 20) and r["token"] != c.s["token"]:
            app.db.x("delete from sessions where token=?", (r["token"],))
            return {"ok": True}
    raise ApiError(404, "no_session", "Сеанс не найден")


@route("POST", "/api/sessions/terminate_others")
def terminate_others(app, c):
    app.db.x("delete from sessions where user_id=? and token!=?", (c.u["id"], c.s["token"]))
    return {"ok": True}


@route("POST", "/api/logout")
def logout(app, c):
    app.db.x("delete from sessions where token=?", (c.s["token"],))
    return {"ok": True}


# ------------------------------------------------------------------ chat preferences (pin / mute / archive) and drafts
@route("POST", "/api/chats/prefs")
def chats_prefs(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if not m:
        raise ApiError(403, "forbidden", "Вы не участник этого чата")
    cur = app.db.one("select * from chat_prefs where user_id=? and chat_id=?", (c.u["id"], ch["id"])) or {"pinned": 0, "muted_until": 0, "archived": 0, "data": "{}"}
    pinned = int(bool(c.body["pinned"])) if "pinned" in c.body else cur["pinned"]
    if pinned and not cur["pinned"] and app.db.one("select count(*) n from chat_prefs where user_id=? and pinned=1", (c.u["id"],))["n"] >= lim(app, c.u, "pinned_chats"):
        raise ApiError(402 if not is_premium(c.u) else 400, "pin_limit", "Лимит закреплённых чатов: %d" % lim(app, c.u, "pinned_chats"))
    app.db.x("insert or replace into chat_prefs(user_id,chat_id,pinned,muted_until,archived,data) values(?,?,?,?,?,?)",
             (c.u["id"], ch["id"], pinned, int(c.body.get("muted_until", cur["muted_until"]) or 0), int(bool(c.body["archived"])) if "archived" in c.body else cur["archived"],
              json.dumps(c.body["data"]) if isinstance(c.body.get("data"), dict) else cur["data"]))
    return {"ok": True}


@route("GET", "/api/chats/prefs")
def chats_prefs_get(app, c):
    return {"prefs": [{"chat_id": r["chat_id"], "pinned": bool(r["pinned"]), "muted_until": r["muted_until"], "archived": bool(r["archived"]), "data": json.loads(r["data"])}
                      for r in app.db.q("select * from chat_prefs where user_id=?", (c.u["id"],))]}


@route("POST", "/api/drafts")
def drafts_set(app, c):
    cid = int(c.body.get("chat_id") or 0)
    if not get_member(app, cid, c.u["id"])[1]:
        raise ApiError(403, "forbidden", "Вы не участник этого чата")
    text = c.body.get("text")
    if not isinstance(text, str) or not text:
        app.db.x("delete from drafts where user_id=? and chat_id=?", (c.u["id"], cid))
    else:
        app.db.x("insert or replace into drafts(user_id,chat_id,text,reply_to,ts) values(?,?,?,?,?)", (c.u["id"], cid, text[:60000], int(c.body.get("reply_to") or 0), now()))
    return {"ok": True}


@route("GET", "/api/drafts")
def drafts_get(app, c):
    return {"drafts": [{"chat_id": r["chat_id"], "text": r["text"], "reply_to": r["reply_to"], "ts": r["ts"]} for r in app.db.q("select * from drafts where user_id=?", (c.u["id"],))]}


# ------------------------------------------------------------------ chats: create / join / invites / administration
UNAME_OK = re.compile(r"[A-Za-z][A-Za-z0-9_]{4,31}")


@route("POST", "/api/chats/create")
def chats_create(app, c):
    b, db = c.body, app.db
    kind, title = s(b.get("kind"), 10), s(b.get("title"), 64)
    if kind not in ("group", "channel") or not title:
        raise ApiError(400, "bad_request", "Укажите тип и название")
    pub = bool(b.get("public"))
    uname, key_blob = s(b.get("username"), 40).lstrip("@") or None, s(b.get("key_blob"), 600)
    if pub:
        if not uname or not UNAME_OK.fullmatch(uname):
            raise ApiError(400, "bad_username", "Публичная ссылка: 5–32 символа, латиница, цифры и «_», первая буква — латинская")
        if name_taken(app, uname):
            raise ApiError(409, "username_taken", "Эта ссылка уже занята")
        if db.one("select count(*) n from chats where owner_id=? and username is not null", (c.u["id"],))["n"] >= lim(app, c.u, "public_links"):
            raise ApiError(402, "links_limit", "Лимит публичных ссылок: %d" % lim(app, c.u, "public_links"))
    else:
        uname = None
        if not key_blob:
            key_blob = secrets.token_urlsafe(32)  # auto key for private groups/channels
    rate(app, ("create", c.u["id"]), 10, 3600)
    cid = db.x("insert into chats(kind,title,username,about,public,owner_id,created,noforwards,ttl,invite_hash) values(?,?,?,?,?,?,?,?,?,?)",
               (kind, title, uname, s(b.get("about"), 200), 1 if pub else 0, c.u["id"], now(), int(bool(b.get("noforwards"))), int(b.get("ttl") or 0), secrets.token_urlsafe(12)))
    db.x("insert into members(chat_id,user_id,role,key_blob,key_from,since_msg,joined) values(?,?,?,?,?,?,?)",
         (cid, c.u["id"], "owner", key_blob, c.u["login"] if key_blob else "", 0, now()))
    return {"chat": chat_dict(app, db.one("select * from chats where id=?", (cid,)), c.u["id"])}


def channels_count(app, uid):
    return app.db.one("select count(*) n from members m join chats c on c.id=m.chat_id where m.user_id=? and c.kind='channel'", (uid,))["n"]


@route("POST", "/api/chats/join")
def chats_join(app, c):
    ch = app.db.one("select * from chats where id=?", (int(c.body.get("chat_id") or 0),))
    if not ch or not ch["public"] or ch["banned"]:
        raise ApiError(404, "no_chat", "Публичный чат не найден")
    return join_public(app, c.u, ch)


def join_public(app, u, ch):
    if ch["kind"] == "channel" and not app.db.one("select 1 from members where chat_id=? and user_id=?", (ch["id"], u["id"])) and channels_count(app, u["id"]) >= lim(app, u, "channels"):
        raise ApiError(402, "channels_limit", "Лимит каналов: %d" % lim(app, u, "channels"))
    app.db.x("insert or ignore into members(chat_id,user_id,since_msg,joined) values(?,?,0,?)", (ch["id"], u["id"], now()))
    notify()
    return {"chat": chat_dict(app, ch, u["id"], history=True)}


@route("POST", "/api/chats/invite/new")
def invite_new(app, c):
    ch, _ = need_admin(app, int(c.body.get("chat_id") or 0), c.u["id"])
    h = secrets.token_urlsafe(12)
    app.db.x("update chats set invite_hash=? where id=?", (h, ch["id"]))
    return {"hash": h, "link": "mm://join/" + h, "url": "http://%s/join/%s" % (c.host, h)}


@route("GET", "/api/resolve")
def resolve(app, c):
    q = s(c.q.get("q"), 300)
    m = re.search(r"join/([A-Za-z0-9_-]{8,})", q)
    h = m.group(1) if m else (q if re.fullmatch(r"[A-Za-z0-9_-]{16,}", q) else "")
    db = app.db
    if h:
        ch = db.one("select * from chats where invite_hash=? and banned=0", (h,))
        if ch:
            mem = db.one("select 1 from members where chat_id=? and user_id=?", (ch["id"], c.u["id"]))
            return {"type": "invite", "hash": h, "chat": {"id": ch["id"], "kind": ch["kind"], "title": ch["title"], "about": ch["about"], "public": bool(ch["public"]), "avatar_id": ch["avatar_id"],
                    "members": db.one("select count(*) n from members where chat_id=?", (ch["id"],))["n"]}, "joined": mem is not None}
    n = q.rstrip("/").rsplit("/", 1)[-1].lstrip("@")
    ch = db.one("select * from chats where username=? and banned=0", (n,)) if n else None
    if ch:
        return {"type": "chat", "chat": {"id": ch["id"], "kind": ch["kind"], "title": ch["title"], "about": ch["about"], "public": True, "username": ch["username"], "avatar_id": ch["avatar_id"],
                "members": db.one("select count(*) n from members where chat_id=?", (ch["id"],))["n"]},
                "joined": db.one("select 1 from members where chat_id=? and user_id=?", (ch["id"], c.u["id"])) is not None}
    t = db.one("select * from users where (username=? or login=?) and deleted=0", (n, n)) if n else None
    if t:
        return {"type": "user", "user": {"id": t["id"], "username": t["username"], "name": t["name"], "avatar_id": t["avatar_id"], "is_bot": bool(t["is_bot"])}}
    raise ApiError(404, "not_found", "Ничего не найдено")


@route("POST", "/api/chats/join_invite")
def join_invite(app, c):
    ch = app.db.one("select * from chats where invite_hash=? and banned=0", (s(c.body.get("hash"), 80),))
    if not ch:
        raise ApiError(404, "bad_invite", "Ссылка-приглашение недействительна")
    if app.db.one("select 1 from members where chat_id=? and user_id=?", (ch["id"], c.u["id"])):
        return {"chat": chat_dict(app, ch, c.u["id"], history=True)}
    if ch["public"]:
        return join_public(app, c.u, ch)
    # private (end-to-end) chat: the server cannot hand out the group key; an admin must approve and wrap it
    app.db.x("insert or ignore into join_requests(chat_id,user_id,ts) values(?,?,?)", (ch["id"], c.u["id"], now()))
    emit(app, ch["id"], "join_request", {"user_id": c.u["id"], "username": c.u["username"], "name": c.u["name"], "pub": c.u["pub"]})
    return {"pending": True, "message": "Запрос отправлен администраторам"}


@route("GET", "/api/chats/join_requests")
def join_requests(app, c):
    need_admin(app, int(c.q.get("chat_id") or 0), c.u["id"])
    return {"requests": [{"user_id": r["id"], "username": r["username"], "name": r["name"], "pub": r["pub"], "ts": r["ts"]} for r in
                         app.db.q("select u.id,u.username,u.name,u.pub,j.ts from join_requests j join users u on u.id=j.user_id where j.chat_id=?", (int(c.q["chat_id"]),))]}


@route("POST", "/api/chats/approve")
def join_approve(app, c):
    ch, _ = need_admin(app, int(c.body.get("chat_id") or 0), c.u["id"])
    t = user_by_login(app, c.body.get("login"))
    if not app.db.one("select 1 from join_requests where chat_id=? and user_id=?", (ch["id"], t["id"])):
        raise ApiError(404, "no_request", "Запрос не найден")
    app.db.x("delete from join_requests where chat_id=? and user_id=?", (ch["id"], t["id"]))
    if c.body.get("decline"):
        return {"ok": True}
    blob = s(c.body.get("key_blob"), 600)
    if not blob:
        raise ApiError(400, "no_key", "Нужен ключ группы, зашифрованный для участника")
    app.db.x("insert or ignore into members(chat_id,user_id,key_blob,key_from,since_msg,joined) values(?,?,?,?,?,?)", (ch["id"], t["id"], blob, c.u["login"], app.max_msg(), now()))
    notify()
    return {"ok": True}


@route("POST", "/api/chats/add_member")
def chats_add_member(app, c):
    ch, m = need_admin(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if ch["kind"] not in ("group", "channel"):
        raise ApiError(400, "bad_chat", "Сюда нельзя добавлять участников")
    t = user_by_login(app, c.body.get("login"))
    if t["is_bot"] and not ch["public"]:
        raise ApiError(400, "bot_e2e", "Боты не могут участвовать в чатах со сквозным шифрованием")
    if is_blocked(app, t["id"], c.u["id"]) or not can(app, t, c.u["id"], "invites"):
        raise ApiError(403, "privacy", "Пользователь запретил приглашать себя в группы и каналы")
    blob = s(c.body.get("key_blob"), 600)
    if not ch["public"] and not blob:
        blob = secrets.token_urlsafe(32)
    app.db.x("insert or ignore into members(chat_id,user_id,key_blob,key_from,since_msg,joined) values(?,?,?,?,?,?)",
             (ch["id"], t["id"], blob, c.u["login"] if blob else "", 0 if ch["public"] else app.max_msg(), now()))
    notify()
    if t["is_bot"]:
        import ext_bots
        ext_bots.on_added(app, ch, t, c.u)
    return {"ok": True}


@route("POST", "/api/chats/set_role")
def set_role(app, c):
    ch, _ = need_admin(app, int(c.body.get("chat_id") or 0), c.u["id"], owner_only=True)
    t = user_by_login(app, c.body.get("login"))
    role = s(c.body.get("role"), 10)
    if role not in ("admin", "moderator", "member"):
        raise ApiError(400, "bad_role", "Роль: admin, moderator или member")
    app.db.x("update members set role=? where chat_id=? and user_id=? and role!='owner'", (role, ch["id"], t["id"]))
    emit(app, ch["id"], "role", {"user_id": t["id"], "role": role})
    return {"ok": True}


@route("POST", "/api/chats/remove_member")
def remove_member(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    t = user_by_login(app, c.body.get("login"))
    if not m or m["role"] not in ("owner", "admin", "moderator"):
        raise ApiError(403, "forbidden", "Недостаточно прав")
    tm = app.db.one("select * from members where chat_id=? and user_id=?", (ch["id"], t["id"]))
    if not tm or tm["role"] == "owner" or (tm["role"] in ("admin", "moderator") and m["role"] != "owner"):
        raise ApiError(403, "forbidden", "Нельзя исключить этого участника")
    app.db.x("delete from members where chat_id=? and user_id=?", (ch["id"], t["id"]))
    emit(app, ch["id"], "member_removed", {"user_id": t["id"]})
    return {"ok": True}


@route("POST", "/api/chats/update")
def chats_update(app, c):
    ch, _ = need_admin(app, int(c.body.get("chat_id") or 0), c.u["id"])
    b, sets, args = c.body, [], []
    if "title" in b and s(b["title"], 64):
        sets.append("title=?"); args.append(s(b["title"], 64))
    if "about" in b:
        sets.append("about=?"); args.append(s(b["about"], 200))
    if "noforwards" in b:
        sets.append("noforwards=?"); args.append(int(bool(b["noforwards"])))
    if "ttl" in b:
        sets.append("ttl=?"); args.append(max(0, min(int(b["ttl"] or 0), 31536000)))
    if "slowmode" in b:
        sets.append("slowmode=?"); args.append(max(0, min(int(b["slowmode"] or 0), 3600)))
    if "avatar_id" in b:
        sets.append("avatar_id=?"); args.append(int(b["avatar_id"] or 0))
    if "paid_stars" in b:
        if ch["kind"] != "channel" or not ch["public"]:
            raise ApiError(400, "bad_chat", "Платный доступ возможен только в публичных каналах")
        sets.append("paid_stars=?"); args.append(max(0, int(b["paid_stars"] or 0)))
    if "username" in b and ch["public"]:
        un = s(b["username"], 40).lstrip("@")
        if not UNAME_OK.fullmatch(un) or (un.lower() != (ch["username"] or "").lower() and name_taken(app, un)):
            raise ApiError(409, "username_taken", "Некорректная или занятая ссылка")
        sets.append("username=?"); args.append(un)
    if sets:
        app.db.x("update chats set %s where id=?" % ",".join(sets), (*args, ch["id"]))
        emit(app, ch["id"], "chat_update", {k: b[k] for k in b if k != "chat_id"})
    return {"chat": chat_dict(app, app.db.one("select * from chats where id=?", (ch["id"],)), c.u["id"])}


@route("POST", "/api/chats/delete")
def chats_delete(app, c):
    ch, _ = need_admin(app, int(c.body.get("chat_id") or 0), c.u["id"], owner_only=True)
    if ch["kind"] not in ("group", "channel"):
        raise ApiError(400, "bad_chat", "Нельзя удалить этот чат")
    for t in ("messages", "members", "reports", "events", "chat_prefs", "drafts", "read_state", "join_requests"):
        app.db.x("delete from %s where chat_id=?" % t, (ch["id"],))
    app.db.x("delete from chats where id=?", (ch["id"],))
    return {"ok": True}


@route("GET", "/api/chats/members")
def chats_members(app, c):
    ch, m = get_member(app, int(c.q.get("chat_id") or 0), c.u["id"])
    if not (m or ch["public"]):
        raise ApiError(403, "forbidden", "Нет доступа")
    rows = app.db.q("select u.id,u.username login,u.name,u.avatar_id,u.is_bot,mm.role from members mm join users u on u.id=mm.user_id where mm.chat_id=? order by mm.role='owner' desc, u.name limit 200", (ch["id"],))
    return {"members": [dict(r) for r in rows]}
