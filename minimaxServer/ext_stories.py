
"""Stories: publish requires Premium; viewing is free."""
import json, time
from api import ApiError, is_premium, now, route, s, emit


def ensure_schema(db):
    db.c.execute("""create table if not exists stories(
        id integer primary key autoincrement, user_id integer not null, media_id integer not null default 0,
        body text not null default '', created integer not null, expires integer not null)""")
    db.c.execute("""create table if not exists story_views(
        story_id integer not null, viewer_id integer not null, ts integer, primary key(story_id, viewer_id))""")


@route("GET", "/api/stories/feed")
def stories_feed(app, c):
    ensure_schema(app.db)
    t = now()
    rows = app.db.q(
        "select s.*, u.login, u.name, u.avatar_id from stories s join users u on u.id=s.user_id "
        "where s.expires>? and u.deleted=0 order by s.created desc limit 100", (t,))
    return {"stories": [{
        "id": r["id"], "user_id": r["user_id"], "login": r["login"], "name": r["name"],
        "avatar_id": r["avatar_id"], "media_id": r["media_id"], "body": r["body"],
        "created": r["created"], "expires": r["expires"],
        "views": app.db.one("select count(*) n from story_views where story_id=?", (r["id"],))["n"]
    } for r in rows]}


@route("POST", "/api/stories/publish")
def stories_publish(app, c):
    ensure_schema(app.db)
    if not is_premium(app, c.u):
        raise ApiError(402, "premium_required", "Публикация историй доступна только с Premium")
    body = s(c.body.get("body"), 500)
    media_id = int(c.body.get("media_id") or 0)
    if not body and not media_id:
        raise ApiError(400, "empty", "Пустая история")
    t = now()
    sid = app.db.x("insert into stories(user_id,media_id,body,created,expires) values(?,?,?,?,?)",
                   (c.u["id"], media_id, body, t, t + 86400))
    return {"id": sid, "expires": t + 86400}


@route("POST", "/api/stories/view")
def stories_view(app, c):
    ensure_schema(app.db)
    sid = int(c.body.get("story_id") or 0)
    if not app.db.one("select 1 from stories where id=? and expires>?", (sid, now())):
        raise ApiError(404, "gone", "История не найдена")
    app.db.x("insert or ignore into story_views(story_id,viewer_id,ts) values(?,?,?)", (sid, c.u["id"], now()))
    return {"ok": True}
