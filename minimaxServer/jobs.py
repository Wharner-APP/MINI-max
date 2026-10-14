"""Background jobs: scheduled messages, self-destruct purge, poll closing, missed calls, webhook delivery, session / account / media cleanup."""
import json, logging, threading, time
from pathlib import Path

import api, ext_bots, ext_calls, ext_msgs
from api import emit, now, privacy_of

log = logging.getLogger("mm.jobs")


def expire_messages(app):
    rows = app.db.q("select id,chat_id from messages where expires_at>0 and expires_at<=? and deleted=0 limit 500", (now(),))
    by = {}
    for r in rows:
        by.setdefault(r["chat_id"], []).append(r["id"])
    for cid, ids in by.items():
        ch = app.db.one("select * from chats where id=?", (cid,))
        if ch and ch["kind"] == "secret":
            for i in ids:
                app.db.x("delete from messages where id=?", (i,))
        elif ch:
            ext_msgs.wipe(app, ch, ids)


def close_polls(app):
    for p in app.db.q("select p.msg_id, m.chat_id from polls p join messages m on m.id=p.msg_id where p.closed=0 and p.closes_at>0 and p.closes_at<=?", (now(),)):
        app.db.x("update polls set closed=1 where msg_id=?", (p["msg_id"],))
        emit(app, p["chat_id"], "poll", {"msg_id": p["msg_id"], "poll": ext_msgs.poll_dict(app, p["msg_id"], 0)})


def hourly(app):
    t = now()
    app.db.x("delete from messages where target_device!='' and ts<?", (t - 30 * 86400,))             # un-acked secret messages
    app.db.x("delete from inline_results where ts<?", (t - 86400,))
    app.db.x("delete from bot_updates where ts<?", (t - 7 * 86400,))
    for r in app.db.q("select token,user_id from sessions where last<?", (t - 7 * 86400,)):
        if t - app.db.one("select last from sessions where token=?", (r["token"],))["last"] > privacy_of(app, r["user_id"])["session_ttl_days"] * 86400:
            app.db.x("delete from sessions where token=?", (r["token"],))
    gc_media(app)


def daily(app):
    import console
    t = now()
    for u in app.db.q("select * from users where deleted=0 and is_bot=0 and last_seen>0 and last_seen<?", (t - 30 * 86400,)):
        if t - u["last_seen"] > privacy_of(app, u["id"])["delete_after_months"] * 30 * 86400:
            console.anonymize(app, u)
            log.info("inactive account deleted: %s", u["id"])


def gc_media(app):
    t = now()
    for m in app.db.q(
            "select * from media where created<? and (ready=0 or (not exists(select 1 from messages where media_id=media.id) and not exists(select 1 from users where avatar_id=media.id) "
            "and not exists(select 1 from chats where avatar_id=media.id) and not exists(select 1 from stickers where media_id=media.id) and not exists(select 1 from gif_library where media_id=media.id) "
            "and not exists(select 1 from saved_gifs where media_id=media.id) and not exists(select 1 from messages where meta like '%\"thumb_id\":'||media.id||'%' or meta like '%\"thumb_id\": '||media.id||'%') "
            "and not exists(select 1 from invoices i join messages mm on mm.id=i.msg_id where mm.meta like '%\"photo_media_id\": '||media.id||'%')))", (t - 86400,)):
        app.db.x("delete from media where id=?", (m["id"],))
        if m["path"]:
            Path(m["path"]).unlink(missing_ok=True)


def tick(app, sec=0):
    ext_msgs.deliver_scheduled(app)
    expire_messages(app)
    close_polls(app)
    ext_calls.expire_ringing(app)


def _loop(app):
    n = 0
    while True:
        try:
            tick(app)
            if n % 3600 == 60:
                hourly(app)
            if n % 86400 == 120:
                daily(app)
        except Exception:
            log.exception("job failed")
        n += 1
        time.sleep(1)


def _webhooks(app):
    while True:
        try:
            ext_bots.webhook_tick(app)
        except Exception:
            log.exception("webhook failed")
        time.sleep(1)


def start(app):
    for fn in (_loop, _webhooks):
        threading.Thread(target=fn, args=(app,), daemon=True).start()
