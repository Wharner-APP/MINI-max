"""Voice / video / group calls and screen sharing: WebRTC signalling relay (offer/answer/ICE) + ICE server credentials (STUN, coturn REST-API TURN).
The server never touches media; RTP flows peer-to-peer (DTLS-SRTP) or through your TURN server."""
import base64, hashlib, hmac, time

from api import ApiError, can, get_member, is_blocked, notify, now, route, s, user_by_login
from ext_msgs import push_eph

SIGNALS = ("offer", "answer", "ice", "screen_start", "screen_stop", "mute", "video", "e2e_key", "renegotiate")


def call_dict(app, cl, uid=0):
    d = {"id": cl["id"], "chat_id": cl["chat_id"], "caller_id": cl["caller_id"], "callee_id": cl["callee_id"], "video": bool(cl["video"]), "group": bool(cl["grp"]), "state": cl["state"],
         "created": cl["created"], "started": cl["started"], "ended": cl["ended"]}
    if cl["grp"]:
        d["participants"] = [r["user_id"] for r in app.db.q("select user_id from call_members where call_id=? and left=0", (cl["id"],))]
    return d


def ice_servers(app, uid):
    cfg = app.cfg.d.get("calls", {})
    out = [{"urls": cfg.get("stun", ["stun:stun.l.google.com:19302"])}]
    if cfg.get("turn_urls") and cfg.get("turn_secret"):
        exp = int(time.time()) + 3600 * 6
        user = "%d:%d" % (exp, uid)
        cred = base64.b64encode(hmac.new(cfg["turn_secret"].encode(), user.encode(), hashlib.sha1).digest()).decode()
        out.append({"urls": cfg["turn_urls"], "username": user, "credential": cred})
    return out


@route("GET", "/api/calls/ice")
def calls_ice(app, c):
    return {"ice_servers": ice_servers(app, c.u["id"])}


def busy(app, uid):
    return app.db.one("select 1 from calls c left join call_members m on m.call_id=c.id and m.user_id=? where c.state in ('ringing','active') and c.grp=0 and (c.caller_id=? or c.callee_id=?) "
                      "union select 1 from call_members m join calls c on c.id=m.call_id where m.user_id=? and m.left=0 and c.state='active' and c.grp=1", (uid, uid, uid, uid)) is not None


@route("POST", "/api/calls/start")
def calls_start(app, c):
    b, db = c.body, app.db
    video = int(bool(b.get("video")))
    if busy(app, c.u["id"]):
        raise ApiError(409, "busy", "Вы уже в звонке")
    if b.get("chat_id"):       # group call in a chat
        ch, m = get_member(app, int(b["chat_id"]), c.u["id"])
        if not m or ch["kind"] not in ("group",):
            raise ApiError(400, "bad_chat", "Групповые звонки доступны в группах")
        live = db.one("select * from calls where chat_id=? and grp=1 and state='active'", (ch["id"],))
        if live:
            return {"call": call_dict(app, live), "ice_servers": ice_servers(app, c.u["id"])}
        cid = db.x("insert into calls(chat_id,caller_id,video,state,grp,created,started) values(?,?,?,?,1,?,?)", (ch["id"], c.u["id"], video, "active", now(), now()))
        db.x("insert into call_members(call_id,user_id,joined) values(?,?,?)", (cid, c.u["id"], now()))
        for r in db.q("select user_id from members where chat_id=? and user_id!=? limit 200", (ch["id"], c.u["id"])):
            push_eph(app, r["user_id"], {"type": "group_call", "call_id": cid, "chat_id": ch["id"], "from": c.u["id"], "username": c.u["username"], "video": bool(video)})
        return {"call": call_dict(app, db.one("select * from calls where id=?", (cid,))), "ice_servers": ice_servers(app, c.u["id"])}
    peer = user_by_login(app, b.get("login"))
    if peer["is_bot"] or peer["id"] == c.u["id"]:
        raise ApiError(400, "bad_peer", "Нельзя позвонить этому пользователю")
    if is_blocked(app, peer["id"], c.u["id"]) or not can(app, peer, c.u["id"], "calls"):
        raise ApiError(403, "privacy", "Пользователь не принимает звонки от вас")
    if busy(app, peer["id"]):
        cid = db.x("insert into calls(caller_id,callee_id,video,state,created,ended) values(?,?,?,?,?,?)", (c.u["id"], peer["id"], video, "busy", now(), now()))
        return {"call": call_dict(app, db.one("select * from calls where id=?", (cid,)))}
    cid = db.x("insert into calls(caller_id,callee_id,video,state,created) values(?,?,?,?,?)", (c.u["id"], peer["id"], video, "ringing", now()))
    push_eph(app, peer["id"], {"type": "call", "event": "incoming", "call_id": cid, "from": c.u["id"], "username": c.u["username"], "name": c.u["name"], "avatar_id": c.u["avatar_id"], "video": bool(video), "pub": c.u["pub"]})
    return {"call": call_dict(app, db.one("select * from calls where id=?", (cid,))), "ice_servers": ice_servers(app, c.u["id"])}


def my_call(app, c, states=None):
    cl = app.db.one("select * from calls where id=?", (int(c.body.get("call_id") or c.q.get("call_id") or 0),))
    if not cl:
        raise ApiError(404, "no_call", "Звонок не найден")
    uid = c.u["id"]
    ok = uid in (cl["caller_id"], cl["callee_id"]) or (cl["grp"] and get_member(app, cl["chat_id"], uid)[1])
    if not ok or (states and cl["state"] not in states):
        raise ApiError(403 if not ok else 409, "bad_call", "Недоступно в текущем состоянии звонка")
    return cl


def other(cl, uid):
    return cl["callee_id"] if uid == cl["caller_id"] else cl["caller_id"]


@route("POST", "/api/calls/accept")
def calls_accept(app, c):
    cl = my_call(app, c, ("ringing",))
    if cl["callee_id"] != c.u["id"]:
        raise ApiError(403, "bad_call", "Принять звонок может только адресат")
    app.db.x("update calls set state='active', started=? where id=?", (now(), cl["id"]))
    push_eph(app, cl["caller_id"], {"type": "call", "event": "accepted", "call_id": cl["id"], "from": c.u["id"], "pub": c.u["pub"]})
    return {"call": call_dict(app, app.db.one("select * from calls where id=?", (cl["id"],))), "ice_servers": ice_servers(app, c.u["id"])}


@route("POST", "/api/calls/decline")
def calls_decline(app, c):
    cl = my_call(app, c, ("ringing",))
    app.db.x("update calls set state='declined', ended=? where id=?", (now(), cl["id"]))
    push_eph(app, other(cl, c.u["id"]), {"type": "call", "event": "declined", "call_id": cl["id"], "from": c.u["id"]})
    return {"ok": True}


@route("POST", "/api/calls/end")
def calls_end(app, c):
    cl = my_call(app, c)
    if cl["grp"]:
        app.db.x("update call_members set left=? where call_id=? and user_id=? and left=0", (now(), cl["id"], c.u["id"]))
        if not app.db.one("select 1 from call_members where call_id=? and left=0", (cl["id"],)):
            app.db.x("update calls set state='ended', ended=? where id=?", (now(), cl["id"]))
        for r in app.db.q("select user_id from call_members where call_id=? and left=0", (cl["id"],)):
            push_eph(app, r["user_id"], {"type": "call", "event": "left", "call_id": cl["id"], "from": c.u["id"]})
        return {"ok": True}
    if cl["state"] in ("ringing", "active"):
        app.db.x("update calls set state=?, ended=? where id=?", ("ended" if cl["state"] == "active" else "cancelled", now(), cl["id"]))
        push_eph(app, other(cl, c.u["id"]), {"type": "call", "event": "ended", "call_id": cl["id"], "from": c.u["id"]})
    return {"ok": True}


@route("POST", "/api/calls/join")
def calls_join(app, c):
    cl = my_call(app, c, ("active",))
    if not cl["grp"]:
        raise ApiError(400, "bad_call", "Это не групповой звонок")
    n = app.db.one("select count(*) n from call_members where call_id=? and left=0", (cl["id"],))["n"]
    if n >= int(app.cfg.get("calls", "max_group", 8)):
        raise ApiError(409, "call_full", "В звонке максимум %d участников (mesh)" % int(app.cfg.get("calls", "max_group", 8)))
    app.db.x("insert into call_members(call_id,user_id,joined) values(?,?,?) on conflict(call_id,user_id) do update set left=0, joined=excluded.joined", (cl["id"], c.u["id"], now()))
    others = [r["user_id"] for r in app.db.q("select user_id from call_members where call_id=? and left=0 and user_id!=?", (cl["id"], c.u["id"]))]
    for uid in others:
        push_eph(app, uid, {"type": "call", "event": "joined", "call_id": cl["id"], "from": c.u["id"], "username": c.u["username"]})
    return {"call": call_dict(app, cl), "participants": others, "ice_servers": ice_servers(app, c.u["id"])}


@route("POST", "/api/calls/signal")
def calls_signal(app, c):
    cl = my_call(app, c, ("ringing", "active"))
    kind, data = s(c.body.get("type"), 20), c.body.get("data")
    if kind not in SIGNALS or len(str(data)) > 65536:
        raise ApiError(400, "bad_signal", "Недопустимый сигнал")
    if cl["grp"]:
        to = int(c.body.get("to") or 0)
        if not app.db.one("select 1 from call_members where call_id=? and user_id=? and left=0", (cl["id"], to)):
            raise ApiError(404, "no_participant", "Участник не в звонке")
    else:
        to = other(cl, c.u["id"])
    push_eph(app, to, {"type": "call_signal", "call_id": cl["id"], "from": c.u["id"], "kind": kind, "data": data})
    return {"ok": True}


@route("POST", "/api/calls/rate")
def calls_rate(app, c):
    cl = my_call(app, c)
    app.db.x("update calls set rating=? where id=?", (max(1, min(int(c.body.get("rating") or 0), 5)), cl["id"]))
    return {"ok": True}


@route("GET", "/api/calls/state")
def calls_state(app, c):
    return {"call": call_dict(app, my_call(app, c))}


@route("GET", "/api/calls/history")
def calls_history(app, c):
    rows = app.db.q("select * from calls where (caller_id=? or callee_id=?) and grp=0 order by id desc limit 100", (c.u["id"], c.u["id"]))
    return {"calls": [dict(call_dict(app, r), incoming=r["callee_id"] == c.u["id"], duration=(r["ended"] - r["started"]) if r["started"] and r["ended"] else 0,
                           peer=app.db.one("select id,username,name from users where id=?", (other(r, c.u["id"]),))["username"]) for r in rows]}


def expire_ringing(app):
    for cl in app.db.q("select * from calls where state='ringing' and created<?", (now() - int(app.cfg.get("calls", "ring_seconds", 45)),)):
        app.db.x("update calls set state='missed', ended=? where id=?", (now(), cl["id"]))
        for uid in (cl["caller_id"], cl["callee_id"]):
            push_eph(app, uid, {"type": "call", "event": "missed", "call_id": cl["id"]})
