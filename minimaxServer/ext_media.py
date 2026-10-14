"""Media storage (photo, video, file, voice, round video, sticker, GIF, avatar): streaming upload, resumable chunks, Range downloads,
sticker packs, GIF library / saved GIFs.  Private chats: the client encrypts the file BEFORE upload (enc=1) and puts the key in the encrypted message."""
import hashlib, json, os, re, secrets, urllib.parse, urllib.request
from pathlib import Path

from api import ApiError, get_member, is_premium, lim, now, raw, route, s, user_by_login

KINDS = ("photo", "video", "file", "voice", "round", "audio", "sticker", "gif", "avatar", "thumb")
PUBLIC_KINDS = ("avatar", "sticker", "gif", "thumb")


def media_dir(app):
    d = app.cfg.path_of("media", "dir", "data/media")
    d.mkdir(parents=True, exist_ok=True)
    return d


def media_path(app, mid):
    d = media_dir(app) / ("%02d" % (mid % 100))
    d.mkdir(exist_ok=True)
    return d / str(mid)


def media_dict(m):
    return {"id": m["id"], "kind": m["kind"], "name": m["name"], "mime": m["mime"], "size": m["size"], "w": m["w"], "h": m["h"], "dur": m["dur"], "enc": bool(m["enc"]),
            "meta": json.loads(m["meta"] or "{}")}


def max_bytes(app, u):
    return min(lim(app, u, "upload_mb"), int(app.cfg.get("media", "max_mb", 4096))) * 1024 * 1024


def user_can_read_media(app, u, m):
    """owner / public kinds / attached to a message in a chat the user belongs to (or a public chat), honouring paid posts"""
    if m["owner_id"] == u["id"] or m["kind"] in PUBLIC_KINDS:
        return True
    for r in app.db.q("select m.id,m.chat_id,m.paid_stars,m.sender_id,c.public from messages m join chats c on c.id=m.chat_id where m.media_id=? and m.deleted=0", (m["id"],)):
        mem = app.db.one("select role from members where chat_id=? and user_id=?", (r["chat_id"], u["id"]))
        if not (mem or r["public"]):
            continue
        if r["paid_stars"] and r["sender_id"] != u["id"] and not (mem and mem["role"] in ("owner", "admin")) and \
                not app.db.one("select 1 from paid_unlocks where user_id=? and msg_id=?", (u["id"], r["id"])):
            continue
        return True
    return False


def media_ok(app, u, mid):
    m = app.db.one("select * from media where id=? and ready=1", (mid,))
    return m if m and user_can_read_media(app, u, m) else None


def _new(app, u, q):
    kind = s(q.get("kind"), 10) or "file"
    if kind not in KINDS:
        raise ApiError(400, "bad_kind", "Неизвестный тип файла")
    num = lambda k: max(0, min(int(q.get(k) or 0), 10 ** 7))
    meta = {}
    try:
        meta = json.loads(q["meta"]) if q.get("meta") else {}
    except ValueError:
        pass
    if not isinstance(meta, dict) or len(json.dumps(meta)) > 8000:
        meta = {}
    return app.db.x("insert into media(owner_id,kind,name,mime,w,h,dur,enc,meta,created) values(?,?,?,?,?,?,?,?,?,?)",
                    (u["id"], kind, s(q.get("name"), 200), s(q.get("mime"), 100) or "application/octet-stream", num("w"), num("h"), num("dur"), int(q.get("enc") in (1, "1", True, "true")),
                     json.dumps(meta), now()))


def _stream(h, path, n, mode="wb"):
    sha = hashlib.sha256()
    left = n
    with open(path, mode) as f:
        while left > 0:
            b = h.rfile.read(min(1 << 20, left))
            if not b:
                raise ApiError(400, "short_body", "Загрузка прервана")
            f.write(b)
            sha.update(b)
            left -= len(b)
    return sha


@raw("POST", "/media/upload")
def upload(h, app, c, path):
    h._auth(app, c)
    q = {k: v[0] for k, v in __import__("urllib.parse").parse.parse_qs(h.path.split("?", 1)[1] if "?" in h.path else "").items()}
    n = int(h.headers.get("Content-Length") or 0)
    if n <= 0:
        raise ApiError(400, "empty", "Пустой файл")
    if n > max_bytes(app, c.u):
        h.drain(n)
        raise ApiError(413, "too_large", "Файл больше лимита (%d МБ%s)" % (max_bytes(app, c.u) // 2 ** 20, "" if is_premium(c.u) else "; с Premium — больше"))
    mid = _new(app, c.u, q)
    p = media_path(app, mid)
    try:
        sha = _stream(h, p, n)
    except Exception:
        app.db.x("delete from media where id=?", (mid,))
        p.unlink(missing_ok=True)
        raise
    app.db.x("update media set size=?, path=?, sha256=?, ready=1 where id=?", (n, str(p), sha.hexdigest(), mid))
    h._json(200, {"id": mid, "size": n})


@route("POST", "/api/media/init")
def init(app, c):
    size = int(c.body.get("size") or 0)
    if size <= 0 or size > max_bytes(app, c.u):
        raise ApiError(413, "too_large", "Недопустимый размер файла")
    mid = _new(app, c.u, c.body)
    app.db.x("update media set path=? where id=?", (str(media_path(app, mid)), mid))
    app.db.x("update media set meta=json_set(coalesce(nullif(meta,''),'{}'),'$._expected',?) where id=?", (size, mid))
    return {"id": mid, "offset": 0}


@raw("PUT", "/media/chunk")
def chunk(h, app, c, path):
    h._auth(app, c)
    q = {k: v[0] for k, v in __import__("urllib.parse").parse.parse_qs(h.path.split("?", 1)[1] if "?" in h.path else "").items()}
    m = app.db.one("select * from media where id=? and owner_id=? and ready=0", (int(q.get("id") or 0), c.u["id"]))
    if not m:
        raise ApiError(404, "no_media", "Загрузка не найдена")
    p = Path(m["path"])
    cur = p.stat().st_size if p.exists() else 0
    if int(q.get("offset") or 0) != cur:
        return h._json(409, {"error": "bad_offset", "message": "Смещение не совпадает", "offset": cur})
    n = int(h.headers.get("Content-Length") or 0)
    if cur + n > max_bytes(app, c.u):
        h.drain(n)
        raise ApiError(413, "too_large", "Файл больше лимита")
    _stream(h, p, n, "ab")
    h._json(200, {"offset": cur + n})


@route("POST", "/api/media/finish")
def finish(app, c):
    m = app.db.one("select * from media where id=? and owner_id=? and ready=0", (int(c.body.get("id") or 0), c.u["id"]))
    if not m:
        raise ApiError(404, "no_media", "Загрузка не найдена")
    p = Path(m["path"])
    size = p.stat().st_size if p.exists() else 0
    exp = json.loads(m["meta"] or "{}").get("_expected")
    if not size or (exp and exp != size):
        raise ApiError(400, "incomplete", "Файл загружен не полностью (%d из %s)" % (size, exp))
    sha = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            sha.update(b)
    app.db.x("update media set size=?, sha256=?, ready=1 where id=?", (size, sha.hexdigest(), m["id"]))
    return {"id": m["id"], "size": size}


@route("GET", "/api/media/info")
def info(app, c):
    m = media_ok(app, c.u, int(c.q.get("id") or 0))
    if not m:
        raise ApiError(404, "no_media", "Файл не найден или нет доступа")
    return media_dict(m)


@raw("GET", "/media/")
def download(h, app, c, path):
    h._auth(app, c, token=__import__("urllib.parse").parse.parse_qs(h.path.split("?", 1)[1] if "?" in h.path else "").get("token", [None])[0])
    tail = path[len("/media/"):]
    if not tail.isdigit():
        raise ApiError(404, "not_found", "unknown endpoint")
    m = media_ok(app, c.u, int(tail))
    if not m or not Path(m["path"]).exists():
        raise ApiError(404, "no_media", "Файл не найден или нет доступа")
    h.send_file(m["path"], "application/octet-stream" if m["enc"] else m["mime"], "" if m["enc"] else m["name"], m["size"],
                {"X-Media-Kind": m["kind"], "X-Media-Sha256": m["sha256"]})


@route("POST", "/api/media/delete")
def media_delete(app, c):
    m = app.db.one("select * from media where id=? and owner_id=?", (int(c.body.get("id") or 0), c.u["id"]))
    if m:
        app.db.x("delete from media where id=?", (m["id"],))
        Path(m["path"]).unlink(missing_ok=True)
    return {"ok": True}


# ------------------------------------------------------------------ sticker packs
def pack_dict(app, p, full=False):
    d = {"id": p["id"], "name": p["short_name"], "title": p["title"], "premium": bool(p["premium"]), "official": bool(p["official"]), "kind": p["kind"]}
    st = app.db.q("select s.id,s.media_id,s.emoji,m.w,m.h,m.mime from stickers s join media m on m.id=s.media_id where s.pack_id=? order by s.idx,s.id", (p["id"],))
    d["count"] = len(st)
    if full:
        d["stickers"] = [{"id": r["id"], "media_id": r["media_id"], "emoji": r["emoji"], "w": r["w"], "h": r["h"], "mime": r["mime"]} for r in st]
    elif st:
        d["cover"] = st[0]["media_id"]
    return d


@route("POST", "/api/stickers/create_pack")
def pack_create(app, c):
    name, title = s(c.body.get("name"), 32), s(c.body.get("title"), 64)
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{2,31}", name) or not title:
        raise ApiError(400, "bad_name", "Короткое имя: 3–32 символа (латиница, цифры, _); нужно название")
    if app.db.one("select 1 from sticker_packs where short_name=?", (name,)):
        raise ApiError(409, "taken", "Такое имя набора уже занято")
    pid = app.db.x("insert into sticker_packs(short_name,title,owner_id,premium,kind,created) values(?,?,?,?,?,?)",
                   (name, title, c.u["id"], int(bool(c.body.get("premium"))), "emoji" if c.body.get("kind") == "emoji" else "sticker", now()))
    app.db.x("insert into pack_installs(user_id,pack_id,ts) values(?,?,?)", (c.u["id"], pid, now()))
    return {"pack": pack_dict(app, app.db.one("select * from sticker_packs where id=?", (pid,)))}


def own_pack(app, c):
    p = app.db.one("select * from sticker_packs where short_name=?", (s(c.body.get("name"), 32),))
    if not p or (p["owner_id"] != c.u["id"]):
        raise ApiError(403, "forbidden", "Набор не найден или вы не его автор")
    return p


@route("POST", "/api/stickers/add")
def sticker_add(app, c):
    p = own_pack(app, c)
    m = app.db.one("select * from media where id=? and owner_id=? and ready=1 and kind='sticker'", (int(c.body.get("media_id") or 0), c.u["id"]))
    if not m:
        raise ApiError(400, "bad_media", "Загрузите файл с kind=sticker")
    n = app.db.one("select count(*) n from stickers where pack_id=?", (p["id"],))["n"]
    if n >= 120:
        raise ApiError(400, "pack_full", "В наборе не может быть больше 120 стикеров")
    app.db.x("insert into stickers(pack_id,media_id,emoji,idx) values(?,?,?,?)", (p["id"], m["id"], s(c.body.get("emoji"), 8), n))
    return {"pack": pack_dict(app, p, True)}


@route("POST", "/api/stickers/remove")
def sticker_remove(app, c):
    p = own_pack(app, c)
    app.db.x("delete from stickers where pack_id=? and id=?", (p["id"], int(c.body.get("sticker_id") or 0)))
    return {"ok": True}


@route("POST", "/api/stickers/delete_pack")
def pack_delete(app, c):
    p = own_pack(app, c)
    for t, col in (("stickers", "pack_id"), ("pack_installs", "pack_id")):
        app.db.x("delete from %s where %s=?" % (t, col), (p["id"],))
    app.db.x("delete from sticker_packs where id=?", (p["id"],))
    return {"ok": True}


@route("GET", "/api/stickers/pack")
def pack_get(app, c):
    p = app.db.one("select * from sticker_packs where short_name=?", (s(c.q.get("name"), 32),))
    if not p:
        raise ApiError(404, "no_pack", "Набор не найден")
    return {"pack": pack_dict(app, p, True)}


@route("POST", "/api/stickers/install")
def pack_install(app, c):
    p = app.db.one("select * from sticker_packs where short_name=?", (s(c.body.get("name"), 32),))
    if not p:
        raise ApiError(404, "no_pack", "Набор не найден")
    app.db.x("insert or ignore into pack_installs(user_id,pack_id,ts) values(?,?,?)", (c.u["id"], p["id"], now()))
    return {"ok": True}


@route("POST", "/api/stickers/uninstall")
def pack_uninstall(app, c):
    p = app.db.one("select * from sticker_packs where short_name=?", (s(c.body.get("name"), 32),))
    if p:
        app.db.x("delete from pack_installs where user_id=? and pack_id=?", (c.u["id"], p["id"]))
    return {"ok": True}


@route("GET", "/api/stickers/mine")
def packs_mine(app, c):
    return {"packs": [pack_dict(app, p, True) for p in app.db.q("select p.* from sticker_packs p join pack_installs i on i.pack_id=p.id where i.user_id=? order by i.ts", (c.u["id"],))]}


@route("GET", "/api/stickers/search")
def packs_search(app, c):
    like = "%" + s(c.q.get("q"), 40).replace("%", "") + "%"
    return {"packs": [pack_dict(app, p) for p in app.db.q("select * from sticker_packs where short_name like ? or title like ? limit 30", (like, like))]}


# ------------------------------------------------------------------ GIFs
@route("GET", "/api/gifs/search")
def gifs_search(app, c):
    q = s(c.q.get("q"), 60)
    like = "%" + q.replace("%", "") + "%"
    out = [{"source": "library", "media_id": r["media_id"], "tags": r["tags"]} for r in app.db.q("select * from gif_library where tags like ? order by ts desc limit 30", (like,))]
    # Giphy (preferred)
    gkey = app.cfg.get("gif", "giphy_key", "")
    if gkey and q:
        try:
            url = "https://api.giphy.com/v1/gifs/search?" + urllib.parse.urlencode({"q": q, "api_key": gkey, "limit": 24, "rating": "pg-13"})
            with urllib.request.urlopen(url, timeout=8) as r:
                data = json.loads(r.read()).get("data") or []
                for it in data:
                    imgs = it.get("images") or {}
                    out.append({
                        "source": "giphy",
                        "id": it.get("id"),
                        "url": (imgs.get("original") or {}).get("url"),
                        "preview": (imgs.get("fixed_height_small") or imgs.get("preview_gif") or {}).get("url"),
                        "w": int((imgs.get("original") or {}).get("width") or 0),
                        "h": int((imgs.get("original") or {}).get("height") or 0),
                    })
        except Exception:
            pass
    # Tenor fallback
    key = app.cfg.get("gif", "tenor_key", "")
    if key and q and not any(x.get("source") == "giphy" for x in out):
        try:
            url = "https://tenor.googleapis.com/v2/search?" + urllib.parse.urlencode({"q": q, "key": key, "limit": 20, "media_filter": "gif,tinygif"})
            with urllib.request.urlopen(url, timeout=6) as r:
                for it in (json.loads(r.read()).get("results") or []):
                    f = (it.get("media_formats") or {})
                    out.append({"source": "tenor", "id": it.get("id"), "url": f.get("gif", {}).get("url"), "preview": f.get("tinygif", {}).get("url")})
        except Exception:
            pass
    return {"gifs": out}



@route("POST", "/api/gifs/save")
def gif_save(app, c):
    m = app.db.one("select * from media where id=? and ready=1 and kind in ('gif','video')", (int(c.body.get("media_id") or 0),))
    if not m or not user_can_read_media(app, c.u, m):
        raise ApiError(404, "no_media", "GIF не найдена")
    app.db.x("insert or replace into saved_gifs(user_id,media_id,ts) values(?,?,?)", (c.u["id"], m["id"], now()))
    return {"ok": True}


@route("POST", "/api/gifs/unsave")
def gif_unsave(app, c):
    app.db.x("delete from saved_gifs where user_id=? and media_id=?", (c.u["id"], int(c.body.get("media_id") or 0)))
    return {"ok": True}


@route("GET", "/api/gifs/saved")
def gifs_saved(app, c):
    return {"gifs": [{"media_id": r["media_id"]} for r in app.db.q("select * from saved_gifs where user_id=? order by ts desc limit 200", (c.u["id"],))]}


# ------------------------------------------------------------------ GIF (Giphy) + built-in stickers
@route("GET", "/api/gif/search")
def gif_search(app, c):
    q = s(c.q.get("q"), 80) or "hello"
    key = app.cfg.get("gif", "giphy_key", "") or app.cfg.get("gif", "tenor_key", "")
    if not key:
        return {"results": []}
    import urllib.parse, urllib.request
    url = "https://api.giphy.com/v1/gifs/search?" + urllib.parse.urlencode({
        "api_key": key, "q": q, "limit": 24, "rating": "pg-13", "lang": "ru"
    })
    try:
        with urllib.request.urlopen(url, timeout=8) as r:
            data = json.loads(r.read())
    except Exception as e:
        raise ApiError(502, "gif_upstream", "Giphy: %s" % e)
    out = []
    for g in data.get("data") or []:
        imgs = g.get("images") or {}
        prev = (imgs.get("fixed_height_small") or imgs.get("preview_gif") or imgs.get("downsized") or {})
        full = (imgs.get("downsized_medium") or imgs.get("original") or {})
        out.append({
            "id": g.get("id"),
            "title": g.get("title") or "",
            "preview": prev.get("url") or "",
            "url": full.get("url") or prev.get("url") or "",
            "w": int(full.get("width") or prev.get("width") or 0),
            "h": int(full.get("height") or prev.get("height") or 0),
        })
    return {"results": out}


@route("GET", "/api/gif/trending")
def gif_trending(app, c):
    key = app.cfg.get("gif", "giphy_key", "")
    if not key:
        return {"results": []}
    import urllib.parse, urllib.request
    url = "https://api.giphy.com/v1/gifs/trending?" + urllib.parse.urlencode({"api_key": key, "limit": 24, "rating": "pg-13"})
    try:
        with urllib.request.urlopen(url, timeout=8) as r:
            data = json.loads(r.read())
    except Exception as e:
        raise ApiError(502, "gif_upstream", "Giphy: %s" % e)
    out = []
    for g in data.get("data") or []:
        imgs = g.get("images") or {}
        prev = imgs.get("fixed_height_small") or {}
        full = imgs.get("downsized_medium") or imgs.get("original") or {}
        out.append({"id": g.get("id"), "title": g.get("title") or "", "preview": prev.get("url") or "",
                    "url": full.get("url") or "", "w": int(full.get("width") or 0), "h": int(full.get("height") or 0)})
    return {"results": out}


STICKER_PACKS = [
    {"id": "emotions", "title": "Эмоции", "stickers": [
        "😀","😁","😂","🤣","😃","😄","😅","😆","😉","😊","😋","😎","😍","🥰","😘","😗","😙","😚","🙂","🤗","🤩","🤔","🤨","😐","😑","😶","🙄","😏","😣","😥","😮","🤐","😯","😪","😫","🥱","😴","😌","😛","😜","😝","🤤","😒","😓","😔","😕","🙃","🤑","😲","☹️","🙁","😖","😞","😟","😤","😢","😭","😦","😧","😨","😩","🤯","😬","😰","😱","🥵","🥶","😳","🤪","😵","😡","😠","🤬","😷","🤒","🤕","🤢","🤮","🥴","😇","🥳","🥺","🤠","🤡","🤥","🤫","🤭","🧐","🤓","😈","👿","💀","☠️","👻","👽","🤖","💩","😺","😸","😹","😻","😼","😽","🙀","😿","😾"
    ]},
    {"id": "hearts", "title": "Сердца", "stickers": [
        "❤️","🧡","💛","💚","💙","💜","🖤","🤍","🤎","💔","❣️","💕","💞","💓","💗","💖","💘","💝","💟","♥️","😍","🥰","😘","😻","💌","🌹","🥀"
    ]},
    {"id": "hands", "title": "Жесты", "stickers": [
        "👍","👎","👌","✌️","🤞","🤟","🤘","🤙","👈","👉","👆","👇","☝️","✋","🤚","🖐","🖖","👋","💪","🙏","👏","🙌","👐","🤲","🤝","👊","✊","🤛","🤜","✍️","💅","🤳"
    ]},
    {"id": "animals", "title": "Животные", "stickers": [
        "🐶","🐱","🐭","🐹","🐰","🦊","🐻","🐼","🐨","🐯","🦁","🐮","🐷","🐸","🐵","🐔","🐧","🐦","🐤","🦆","🦅","🦉","🦇","🐺","🐗","🐴","🦄","🐝","🐛","🦋","🐌","🐞","🐜","🦟","🦗","🕷","🦂","🐢","🐍","🦎","🦖","🦕","🐙","🦑","🦐","🦞","🦀","🐡","🐠","🐟","🐬","🐳","🐋","🦈","🐊"
    ]},
    {"id": "food", "title": "Еда", "stickers": [
        "🍎","🍐","🍊","🍋","🍌","🍉","🍇","🍓","🫐","🍈","🍒","🍑","🥭","🍍","🥥","🥝","🍅","🍆","🥑","🥦","🥬","🥒","🌶","🫑","🌽","🥕","🫒","🧄","🧅","🥔","🍠","🥐","🥯","🍞","🥖","🥨","🧀","🥚","🍳","🧈","🥞","🧇","🥓","🥩","🍗","🍖","🦴","🌭","🍔","🍟","🍕","🫓","🥪","🥙","🧆","🌮","🌯","🫔","🥗","🥘","🫕","🍝","🍜","🍲","🍛","🍣","🍱","🥟","🦪","🍤","🍙","🍚","🍘","🍥","🥠","🥮","🍢","🍡","🍧","🍨","🍦","🥧","🧁","🍰","🎂","🍮","🍭","🍬","🍫","🍿","🍩","🍪"
    ]},
    {"id": "flags_misc", "title": "Разное", "stickers": [
        "🔥","⭐","🌟","✨","⚡","💥","💯","🎉","🎊","🎈","🎁","🏆","🥇","🥈","🥉","⚽","🏀","🎮","🎯","🎵","🎶","🎸","🎹","🎺","🎻","🥁","🎬","🎤","🎧","📱","💻","⌚️","📷","💡","📚","✏️","📌","📎","🔗","💰","💎","🔔","🔒","🔑","✈️","🚀","🏠","🌈","☀️","🌙","⛅","❄️","🍀","🌸","🌺","🌻","🌼","🌷"
    ]},
]


@route("GET", "/api/stickers/packs")
def sticker_packs(app, c):
    return {"packs": [{"id": p["id"], "title": p["title"], "count": len(p["stickers"])} for p in STICKER_PACKS]}


@route("GET", "/api/stickers/pack")
def sticker_pack(app, c):
    pid = s(c.q.get("id"), 40)
    for p in STICKER_PACKS:
        if p["id"] == pid:
            return {"id": p["id"], "title": p["title"], "stickers": p["stickers"]}
    return {"id": pid, "title": "", "stickers": []}
