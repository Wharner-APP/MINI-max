"""Secret chats: X3DH prekey bundles + relay for Double Ratchet messages (end-to-end, device-bound, no server history).
Protocol description: docs/E2E.md; reference implementation: tools/ratchet_ref.py."""
import json, secrets

from api import ApiError, get_member, is_blocked, notify, now, route, s, user_by_login, chat_dict
from ext_msgs import push_eph


def need_device(c):
    dev = c.s["device_id"]
    if not dev:
        raise ApiError(400, "no_device", "Передайте device_id при входе (для секретных чатов устройство обязательно)")
    return dev


def verify_sig(sign_pub_b64, msg_b64, sig_b64):
    import base64
    from nacl.signing import VerifyKey
    try:
        VerifyKey(base64.b64decode(sign_pub_b64)).verify(base64.b64decode(msg_b64), base64.b64decode(sig_b64))
        return True
    except Exception:
        return False


@route("POST", "/api/e2e/prekeys")
def prekeys_upload(app, c):
    dev, b = need_device(c), c.body
    spk = b.get("spk") or {}
    for k in ("sign_pub", "dh_pub"):
        if len(s(b.get(k), 100)) < 40:
            raise ApiError(400, "bad_key", "Некорректный ключ %s" % k)
    if not verify_sig(b["sign_pub"], spk.get("pub", ""), spk.get("sig", "")):
        raise ApiError(400, "bad_signature", "Подпись подписанного prekey недействительна")
    app.db.x("insert or replace into e2e_devices(user_id,device_id,sign_pub,dh_pub,spk_id,spk_pub,spk_sig,updated) values(?,?,?,?,?,?,?,?)",
             (c.u["id"], dev, b["sign_pub"], b["dh_pub"], int(spk.get("id") or 0), spk["pub"], spk["sig"], now()))
    for k in (b.get("otks") or [])[:100]:
        app.db.x("insert or replace into e2e_otk(user_id,device_id,key_id,pub) values(?,?,?,?)", (c.u["id"], dev, int(k["id"]), s(k["pub"], 100)))
    n = app.db.one("select count(*) n from e2e_otk where user_id=? and device_id=?", (c.u["id"], dev))["n"]
    return {"ok": True, "otk_count": n}


@route("GET", "/api/e2e/prekeys/count")
def prekeys_count(app, c):
    return {"otk_count": app.db.one("select count(*) n from e2e_otk where user_id=? and device_id=?", (c.u["id"], c.s["device_id"]))["n"]}


@route("GET", "/api/e2e/devices")
def e2e_devices(app, c):
    t = user_by_login(app, c.q.get("login"))
    return {"devices": [{"device_id": r["device_id"], "updated": r["updated"]} for r in app.db.q("select * from e2e_devices where user_id=? order by updated desc", (t["id"],))]}


@route("GET", "/api/e2e/bundle")
def bundle(app, c):
    t = user_by_login(app, c.q.get("login"))
    dev = s(c.q.get("device_id"), 64)
    d = app.db.one("select * from e2e_devices where user_id=? and device_id=?", (t["id"], dev)) if dev else \
        app.db.one("select * from e2e_devices where user_id=? order by updated desc limit 1", (t["id"],))
    if not d:
        raise ApiError(404, "no_bundle", "У пользователя нет устройства с секретными чатами")
    with app.db.lock:       # a one-time prekey is handed out exactly once
        otk = app.db.one("select * from e2e_otk where user_id=? and device_id=? order by key_id limit 1", (t["id"], d["device_id"]))
        if otk:
            app.db.x("delete from e2e_otk where user_id=? and device_id=? and key_id=?", (t["id"], d["device_id"], otk["key_id"]))
    return {"user_id": t["id"], "device_id": d["device_id"], "sign_pub": d["sign_pub"], "dh_pub": d["dh_pub"], "spk": {"id": d["spk_id"], "pub": d["spk_pub"], "sig": d["spk_sig"]},
            "otk": {"id": otk["key_id"], "pub": otk["pub"]} if otk else None}


@route("POST", "/api/secret/create")
def secret_create(app, c):
    dev = need_device(c)
    peer = user_by_login(app, c.body.get("login"))
    pdev = s(c.body.get("device_id"), 64)
    if peer["id"] == c.u["id"] or peer["is_bot"] or is_blocked(app, peer["id"], c.u["id"]):
        raise ApiError(403, "forbidden", "Секретный чат с этим пользователем невозможен")
    if not pdev or not app.db.one("select 1 from e2e_devices where user_id=? and device_id=?", (peer["id"], pdev)):
        raise ApiError(404, "no_device", "Устройство собеседника не найдено")
    init = c.body.get("init")
    if not isinstance(init, dict) or len(json.dumps(init)) > 4000:
        raise ApiError(400, "bad_init", "Нужны данные инициализации X3DH (ephemeral key, id использованных prekey)")
    cid = app.db.x("insert into chats(kind,dm_key,owner_id,created,init,ttl) values('secret',?,?,?,?,?)", ("s:" + secrets.token_hex(8), c.u["id"], now(), json.dumps(init), int(c.body.get("ttl") or 0)))
    app.db.x("insert into members(chat_id,user_id,joined,device_id) values(?,?,?,?)", (cid, c.u["id"], now(), dev))
    app.db.x("insert into members(chat_id,user_id,joined,device_id) values(?,?,?,?)", (cid, peer["id"], now(), pdev))
    notify()
    return {"chat": chat_dict(app, app.db.one("select * from chats where id=?", (cid,)), c.u["id"])}


@route("POST", "/api/secret/ack")
def secret_ack(app, c):
    """The receiving device confirms it stored the messages -> the server deletes them (no server-side history for secret chats)."""
    dev = need_device(c)
    ids = [int(x) for x in (c.body.get("ids") or [])][:500]
    n = 0
    for mid in ids:
        r = app.db.one("select m.id from messages m join members mm on mm.chat_id=m.chat_id and mm.user_id=? where m.id=? and m.target_device=?", (c.u["id"], mid, dev))
        if r:
            app.db.x("delete from messages where id=?", (mid,))
            n += 1
    return {"deleted": n}


@route("GET", "/api/secret/pending")
def secret_pending(app, c):
    """Messages for this device that were not acknowledged yet (even if the sync cursor moved past them)."""
    import ext_msgs
    dev = need_device(c)
    rows = app.db.q(ext_msgs.SEL + " where m.target_device=? and m.deleted=0 order by m.id limit 500", (dev,))
    return {"messages": [ext_msgs.full_msg(app, r, c.u["id"]) for r in rows]}


@route("POST", "/api/secret/delete")
def secret_delete(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if not m or ch["kind"] != "secret":
        raise ApiError(404, "no_chat", "Секретный чат не найден")
    for t in ("messages", "members", "events", "read_state"):
        app.db.x("delete from %s where chat_id=?" % t, (ch["id"],))
    app.db.x("delete from chats where id=?", (ch["id"],))
    return {"ok": True}
