"""Stars ledger, gifts (catalog, supply, rarity, upgrade, convert), test payment gateway, tips, paid posts."""
import secrets

import api
from api import ApiError, can, is_blocked, notify, now, route, s, user_by_login, get_member
import ext_msgs


def add_stars(app, uid, amount, kind, ref="", descr="", need=False):
    with app.db.lock:
        bal = app.db.one("select stars from users where id=?", (uid,))["stars"]
        if amount < 0 and bal + amount < 0:
            if need:
                raise ApiError(402, "no_stars", "Недостаточно звёзд: нужно %d, на балансе %d" % (-amount, bal))
            amount = -bal
        app.db.x("update users set stars=stars+? where id=?", (amount, uid))
        app.db.x("insert into stars_tx(user_id,amount,kind,ref,ts,descr) values(?,?,?,?,?,?)", (uid, amount, kind, ref, now(), descr))
    return amount


def commission(app, amount):
    return amount * (100 - int(app.cfg.get("stars", "commission_percent", 0))) // 100


def apply_payment(app, p):
    db, t, cat = app.db, now(), app.catalog
    target = db.one("select * from users where login=? or username=?", (p["recipient"], p["recipient"])) if p["recipient"] else None
    target = target or db.one("select * from users where id=?", (p["user_id"],))
    if p["kind"] == "stars":
        add_stars(app, target["id"], int(p["item"]), "purchase", p["id"], "Покупка звёзд")
    elif p["kind"] == "premium":
        months = next(x["months"] for x in cat["plans"] if x["id"] == p["item"])
        db.x("update users set premium_until=? where id=?", (max(t, target["premium_until"]) + months * 30 * 86400, target["id"]))
    elif p["kind"] == "business":
        db.x("update users set business_until=? where id=?", (max(t, target["business_until"]) + 30 * 86400, target["id"]))
    db.x("update payments set status='paid' where id=?", (p["id"],))


api.apply_payment = apply_payment


def gift_info(app, g):
    sold = app.db.one("select count(*) n from gifts where gift_id=?", (g["id"],))["n"]
    d = dict(g, rarity=g.get("rarity", "common"), sold=sold)
    if g.get("supply"):
        d["remaining"] = max(0, g["supply"] - sold)
    return d


@route("GET", "/api/gifts/catalog")
def gifts_catalog(app, c):
    return {"gifts": [gift_info(app, g) for g in app.catalog["gifts"]]}


@route("POST", "/api/payments/create")
def payments_create(app, c):
    if not app.cfg.get("payments", "test_gateway", True):
        raise ApiError(503, "payments_off", api.PAYMENT_UNAVAILABLE)
    b, cat = c.body, app.catalog
    kind, item, rec = s(b.get("kind"), 12), s(b.get("item"), 40), s(b.get("recipient"), 40)
    to = user_by_login(app, rec) if rec else None
    if kind == "gift":
        g = next((x for x in cat["gifts"] if x["id"] == item), None)
        if not g:
            raise ApiError(400, "bad_item", "Неизвестный подарок")
        if g.get("supply") and gift_info(app, g)["remaining"] <= 0:
            raise ApiError(409, "sold_out", "Подарок распродан")
        to = to or c.u
        if to["id"] != c.u["id"] and (is_blocked(app, to["id"], c.u["id"]) or not can(app, to, c.u["id"], "gifts")):
            raise ApiError(403, "privacy", "Пользователь не принимает подарки")
        add_stars(app, c.u["id"], -g["stars"], "gift_sent", g["id"], "Подарок «%s» для @%s" % (g["name"], to["username"]), need=True)
        gid = app.db.x("insert into gifts(user_id,from_id,gift_id,stars,ts,note,anonymous) values(?,?,?,?,?,?,?)",
                       (to["id"], c.u["id"], g["id"], g["stars"], now(), s(b.get("note"), 200), int(bool(b.get("anonymous")))))
        if to["id"] != c.u["id"]:
            ext_msgs.push_eph(app, to["id"], {"type": "gift", "id": gid, "gift_id": g["id"], "from": None if b.get("anonymous") else c.u["username"], "note": s(b.get("note"), 200), "ts": now()})
        return {"done": True, "id": gid, "message": "Подарок «%s» отправлен: %s" % (g["name"], to["name"])}
    if kind == "stars":
        pk = next((x for x in cat["star_packs"] if str(x["stars"]) == item), None)
        if not pk:
            raise ApiError(400, "bad_item", "Неизвестный пакет звёзд")
        amount = pk["price_minor"]
    elif kind == "premium":
        pl = next((x for x in cat["plans"] if x["id"] == item), None)
        if not pl:
            raise ApiError(400, "bad_item", "Неизвестный тариф")
        amount = pl["price_minor"]
    elif kind == "business":
        amount, item = cat["business_monthly_minor"], "business_1m"
    else:
        raise ApiError(400, "bad_kind", "Неизвестный тип платежа")
    pid = secrets.token_urlsafe(16)
    app.db.x("insert into payments(id,user_id,kind,item,amount_minor,currency,recipient,status,created) values(?,?,?,?,?,?,?,?,?)",
             (pid, c.u["id"], kind, item, amount, cat["currency"], rec, "pending", now()))
    return {"payment_id": pid, "checkout_url": "http://%s/pay/%s" % (c.host, pid)}


@route("GET", "/api/gifts/mine")
def gifts_mine(app, c):
    return {"gifts": [{"id": g["id"], "gift_id": g["gift_id"], "stars": g["stars"], "serial": g["serial"], "ts": g["ts"], "note": g["note"], "hidden": bool(g["hidden"]),
                       "pinned": bool(g["pinned"]), "from": None if g["anonymous"] else g["from_id"]}
                      for g in app.db.q("select * from gifts where user_id=? and converted=0 order by pinned desc,id desc", (c.u["id"],))]}


def my_gift(app, c):
    g = app.db.one("select * from gifts where id=? and user_id=? and converted=0", (int(c.body.get("id") or 0), c.u["id"]))
    if not g:
        raise ApiError(404, "no_gift", "Подарок не найден")
    return g


@route("POST", "/api/gifts/convert")
def gift_convert(app, c):
    g = my_gift(app, c)
    if g["serial"]:
        raise ApiError(400, "upgraded", "Улучшенный подарок нельзя обменять на звёзды")
    back = g["stars"] * int(app.cfg.get("stars", "convert_percent", 85)) // 100
    app.db.x("update gifts set converted=1 where id=?", (g["id"],))
    add_stars(app, c.u["id"], back, "gift_convert", str(g["id"]), "Обмен подарка на звёзды")
    return {"stars": back}


@route("POST", "/api/gifts/upgrade")
def gift_upgrade(app, c):
    g = my_gift(app, c)
    if g["serial"]:
        raise ApiError(400, "upgraded", "Подарок уже улучшен")
    add_stars(app, c.u["id"], -int(app.cfg.get("stars", "upgrade_cost", 50)), "gift_upgrade", str(g["id"]), "Улучшение подарка", need=True)
    n = app.db.one("select coalesce(max(serial),0)+1 n from gifts where gift_id=?", (g["gift_id"],))["n"]
    app.db.x("update gifts set serial=? where id=?", (n, g["id"]))
    return {"serial": n}


@route("POST", "/api/gifts/manage")
def gift_manage(app, c):
    g = my_gift(app, c)
    for k in ("hidden", "pinned"):
        if k in c.body:
            app.db.x("update gifts set %s=? where id=?" % k, (int(bool(c.body[k])), g["id"]))
    return {"ok": True}


@route("GET", "/api/stars/transactions")
def stars_tx(app, c):
    before, limit = int(c.q.get("before") or 0) or 2 ** 62, max(1, min(int(c.q.get("limit") or 50), 100))
    rows = app.db.q("select * from stars_tx where user_id=? and id<? order by id desc limit ?", (c.u["id"], before, limit))
    return {"balance": c.u["stars"], "transactions": [{"id": r["id"], "amount": r["amount"], "kind": r["kind"], "ref": r["ref"], "ts": r["ts"], "descr": r["descr"]} for r in rows]}


@route("GET", "/api/stars/stats")
def stars_stats(app, c):
    q = lambda sql: app.db.one(sql, (c.u["id"],))["n"]
    return {"balance": c.u["stars"], "earned": q("select coalesce(sum(amount),0) n from stars_tx where user_id=? and amount>0 and kind in ('tip_in','unlock_in','invoice_in','paid_message_in')"),
            "spent": -q("select coalesce(sum(amount),0) n from stars_tx where user_id=? and amount<0"), "purchased": q("select coalesce(sum(amount),0) n from stars_tx where user_id=? and kind='purchase'")}


@route("POST", "/api/stars/tip")
def stars_tip(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    amt = int(c.body.get("amount") or 0)
    if not ch["public"] or ch["kind"] not in ("group", "channel") or amt < 1 or amt > 100000:
        raise ApiError(400, "bad_tip", "Донаты возможны в публичных каналах и группах; сумма 1–100000")
    to = app.db.one("select sender_id u from messages where id=? and chat_id=?", (int(c.body.get("msg_id") or 0), ch["id"])) if c.body.get("msg_id") else None
    target = to["u"] if to else ch["owner_id"]
    if target == c.u["id"]:
        raise ApiError(400, "self", "Нельзя отправить звёзды самому себе")
    add_stars(app, c.u["id"], -amt, "tip_out", str(ch["id"]), "Донат", need=True)
    add_stars(app, target, commission(app, amt), "tip_in", str(ch["id"]), "Донат от @" + c.u["username"])
    return {"ok": True}


@route("POST", "/api/messages/unlock")
def messages_unlock(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    msg = app.db.one(ext_msgs.SEL + " where m.id=? and m.chat_id=? and m.deleted=0", (int(c.body.get("msg_id") or 0), ch["id"]))
    if not msg or not msg["paid_stars"]:
        raise ApiError(404, "no_paid_post", "Платный пост не найден")
    if not ext_msgs.is_locked(app, msg, c.u["id"]):
        return {"message": ext_msgs.full_msg(app, msg, c.u["id"])}
    add_stars(app, c.u["id"], -msg["paid_stars"], "unlock_out", str(msg["id"]), "Платный пост", need=True)
    add_stars(app, msg["sender_id"], commission(app, msg["paid_stars"]), "unlock_in", str(msg["id"]), "Доступ к платному посту")
    app.db.x("insert or ignore into paid_unlocks(user_id,msg_id,stars,ts) values(?,?,?,?)", (c.u["id"], msg["id"], msg["paid_stars"], now()))
    return {"message": ext_msgs.full_msg(app, msg, c.u["id"])}
