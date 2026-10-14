"""HTTP API of the MINI max server (stdlib only). Contract: docs/api.md of the client repository + the extensions below.

Privacy model
  * private DMs, private groups and private channels are end-to-end encrypted by the clients (the server only stores
    opaque blobs and refuses plaintext for them);
  * public groups / public channels are plaintext so that moderators can read them from the console.
"""
import base64, hashlib, hmac, json, logging, mimetypes, os, re, secrets, struct, threading, time
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

import captcha

log = logging.getLogger("mm.api")
NEWS = threading.Condition()
ROUTES = {}
RAW = []          # (method, path prefix, fn(handler, app, ctx, path)) - streaming / non-JSON routes
EXTRA_CHAT = []   # hooks that add fields to chat dicts
LOGIN_RE = re.compile(r"[A-Za-z0-9_.]{3,32}")
UNAME_RE = re.compile(r"[A-Za-z][A-Za-z0-9_]{4,31}")
PAYMENT_UNAVAILABLE = "Не удалось получить API платёжной системы. Пожалуйста, попробуйте позже"


class ApiError(Exception):
    def __init__(self, code, error, message=""):
        super().__init__(error)
        self.code, self.error, self.message = code, error, message or error


class Ctx:
    u = s = None
    body = {}
    q = {}
    host = "localhost"


def raw(method, prefix):
    def deco(f):
        RAW.append((method, prefix, f))
        return f
    return deco


def route(method, path, auth=True):
    def deco(f):
        ROUTES[(method, path)] = (f, auth)
        return f
    return deco


def now():
    return int(time.time())


def notify():
    with NEWS:
        NEWS.notify_all()


def emit(app, chat_id, kind, data):
    """Persistent non-message event (edit, delete, reaction, read, pin, ...) delivered to chat members through /api/sync."""
    eid = app.db.x("insert into events(chat_id,kind,data,ts) values(?,?,?,?)", (chat_id, kind, json.dumps(data, ensure_ascii=False), now()))
    notify()
    return eid


def hash_key(auth_key, salt):
    return hashlib.scrypt(auth_key.encode(), salt=salt, n=2 ** 14, r=8, p=1, dklen=32).hex()


def rate(app, key, limit, per):
    t = time.time()
    lst = [x for x in app.rl.get(key, []) if t - x < per]
    if len(lst) >= limit:
        app.rl[key] = lst
        raise ApiError(429, "rate_limited", "Слишком много запросов, подождите немного")
    lst.append(t)
    app.rl[key] = lst


def s(v, mx=256):
    if not isinstance(v, str):
        return ""
    return v.strip()[:mx]


def is_premium(u):
    return u["premium_until"] > now()


def lim(app, u, name):
    d = app.cfg.d.get("premium_limits" if is_premium(u) else "free_limits", {})
    return int(d.get(name, DEFAULT_LIMITS["premium" if is_premium(u) else "free"][name]))


DEFAULT_LIMITS = {
    "free": {"channels": 500, "folders": 10, "pinned_chats": 5, "public_links": 10, "upload_mb": 2048, "reactions": 1, "bio": 70, "caption": 1024},
    "premium": {"channels": 1000, "folders": 30, "pinned_chats": 10, "public_links": 20, "upload_mb": 4096, "reactions": 3, "bio": 140, "caption": 4096},
}
PRIVACY_KEYS = ("last_seen", "photos", "bio", "birthday", "forwards", "calls", "voice", "messages", "gifts", "invites", "read_receipts")
PRIVACY_DEFAULT = {"last_seen": "all", "photos": "all", "bio": "all", "birthday": "contacts", "forwards": "all", "calls": "all", "voice": "all",
                   "messages": "all", "gifts": "all", "invites": "all", "read_receipts": "all"}
SETTING_KINDS = ("privacy", "notifications", "chat", "folders", "ui", "battery", "advanced", "general")


def get_setting(app, uid, kind, default=None):
    r = app.db.one("select data from user_settings where user_id=? and kind=?", (uid, kind))
    if not r:
        return default
    try:
        return json.loads(r["data"])
    except ValueError:
        return default


def privacy_of(app, uid):
    p = dict(get_setting(app, uid, "privacy", {}) or {})
    out = {}
    for k in PRIVACY_KEYS:
        v = p.get(k, PRIVACY_DEFAULT[k])
        out[k] = v if isinstance(v, dict) else {"rule": v}
        out[k].setdefault("rule", PRIVACY_DEFAULT[k])
    out["paid_messages_stars"] = int(p.get("paid_messages_stars", 0) or 0)
    out["delete_after_months"] = int(p.get("delete_after_months", 12) or 12)
    out["session_ttl_days"] = int(p.get("session_ttl_days", 180) or 180)
    return out


def is_contact(app, owner_id, other_id):
    return app.db.one("select 1 from contacts where user_id=? and contact_id=?", (owner_id, other_id)) is not None


def is_blocked(app, blocker_id, other_id):
    return app.db.one("select 1 from blocks where user_id=? and blocked_id=?", (blocker_id, other_id)) is not None


def can(app, owner, viewer_id, key):
    """May `viewer_id` use/see the `key` privacy item of user `owner` (row)?"""
    if owner["id"] == viewer_id:
        return True
    rule = privacy_of(app, owner["id"])[key]
    if viewer_id in rule.get("deny", []):
        return False
    if viewer_id in rule.get("allow", []):
        return True
    r = rule["rule"]
    return r == "all" or (r == "contacts" and is_contact(app, owner["id"], viewer_id))


def name_taken(app, name, exclude_uid=0):
    r = app.db.one("select id from users where (username=? or login=?) and id!=?", (name, name, exclude_uid))
    return r is not None or app.db.one("select 1 from chats where username=?", (name,)) is not None


def pub_user(u):
    return {"id": u["id"], "login": u["login"], "username": u["username"] or u["login"], "display_name": u["name"], "bio": u["bio"],
            "avatar_id": u["avatar_id"], "is_bot": bool(u["is_bot"])}


def me_dict(u):
    return {"id": u["id"], "stars": u["stars"], "premium_until": u["premium_until"], "business_until": u["business_until"],
            "username": u["username"] or u["login"], "avatar_id": u["avatar_id"]}


def msg_dict(r):
    return {"id": r["id"], "chat_id": r["chat_id"], "sender": r["sender"], "sender_name": r["sender_name"], "ts": r["ts"],
            "body": r["body"], "enc": r["enc"], "reply_to": r["reply_to"]}


def chat_dict(app, c, uid, history=False):
    db = app.db
    m = db.one("select * from members where chat_id=? and user_id=?", (c["id"], uid))
    d = {"id": c["id"], "kind": c["kind"], "title": c["title"], "username": c["username"] or "", "about": c["about"],
         "public": bool(c["public"]), "banned": bool(c["banned"]), "role": m["role"] if m else "", "avatar_id": c["avatar_id"],
         "noforwards": bool(c["noforwards"]), "ttl": c["ttl"], "pinned_msg": c["pinned_msg"], "slowmode": c["slowmode"], "paid_stars": c["paid_stars"],
         "members": db.one("select count(*) n from members where chat_id=?", (c["id"],))["n"]}
    if c["kind"] in ("private", "bot", "secret"):
        o = db.one("select u.id,u.username login,u.name,u.pub,u.avatar_id from members mm join users u on u.id=mm.user_id where mm.chat_id=? and mm.user_id!=?",
                   (c["id"], uid))
        if o:
            d["peer"] = {"login": o["login"], "name": o["name"], "pub": o["pub"], "id": o["id"], "avatar_id": o["avatar_id"]}
    if c["kind"] == "secret":
        d["init"] = json.loads(c["init"] or "{}")
        d["device_id"] = m["device_id"] if m else ""
        d["initiator"] = c["owner_id"] == uid
    if m and m["key_blob"]:
        d["key_blob"] = m["key_blob"]
        f = db.one("select pub from users where login=?", (m["key_from"],))
        d["key_from_pub"] = f["pub"] if f else ""
    if history and m:
        rows = db.q("select m.*,u.username sender,u.name sender_name from messages m join users u on u.id=m.sender_id "
                    "where m.chat_id=? and m.id>? and m.deleted=0 and m.target_device='' order by m.id desc limit 50", (c["id"], m["since_msg"]))
        import ext_msgs
        d["history"] = [ext_msgs.full_msg(app, r, uid) for r in reversed(rows)]
    for fn in EXTRA_CHAT:
        fn(app, c, uid, d)
    return d


def max_msg_id(db):
    r = db.one("select coalesce(max(id),0) n from messages")
    return r["n"]


def get_member(app, chat_id, uid):
    c = app.db.one("select * from chats where id=?", (chat_id,))
    if not c:
        raise ApiError(404, "no_chat", "Чат не найден")
    m = app.db.one("select * from members where chat_id=? and user_id=?", (chat_id, uid))
    return c, m


def user_by_login(app, login):
    n = s(login, 40).lstrip("@")
    u = app.db.one("select * from users where username=? and deleted=0", (n,)) or app.db.one("select * from users where login=? and deleted=0", (n,))
    if not u:
        raise ApiError(404, "no_user", "Пользователь не найден")
    return u


# ------------------------------------------------------------------ system
@route("GET", "/api/ping", auth=False)
def ping(app, c):
    return {"ok": True, "server": app.cfg.get("server", "name", "MINI max"), "version": "1"}


@route("GET", "/api/get_captcha", auth=False)
def get_captcha(app, c):
    r = captcha.create(app.cfg)
    if not r:
        raise ApiError(503, "captcha_unavailable", "Капча не настроена на сервере (нужен hCaptcha secret или Pillow)")
    return r


@route("GET", "/api/captcha/status", auth=False)
def captcha_status(app, c):
    return captcha.status(s(c.q.get("id"), 64))


@route("POST", "/api/captcha/verify", auth=False)
def captcha_verify(app, c):
    ok, err = captcha.verify_hcaptcha(app.cfg, s(c.body.get("captcha_id"), 64), s(c.body.get("token"), 4096))
    if not ok:
        raise ApiError(400, "captcha_failed", "Проверка не пройдена: " + err)
    return {"ok": True}


# ------------------------------------------------------------------ accounts
@route("POST", "/api/device/check", auth=False)
def device_check(app, c):
    fp = s(c.body.get("fingerprint"), 128)
    reg = bool(fp) and app.cfg.get("security", "one_account_per_device", True) and \
        app.db.one("select 1 from users where fingerprint=?", (fp,)) is not None
    return {"registered": reg}


def new_session(app, u, device="", c=None):
    tok = secrets.token_hex(24)
    b = c.body if c else {}
    app.db.x("insert into sessions(token,user_id,device,created,last,device_id,name,platform,app,ip) values(?,?,?,?,?,?,?,?,?,?)",
             (tok, u["id"], device, now(), now(), s(b.get("device_id"), 64), s(b.get("device_name"), 60) or device, s(b.get("platform"), 30), s(b.get("app_version"), 30),
              c.ip if c else ""))
    app.db.x("update users set last_seen=? where id=?", (now(), u["id"]))
    return {"token": tok, "user": pub_user(u)}


@route("POST", "/api/register", auth=False)
def register(app, c):
    b = c.body
    login, name, key = s(b.get("login"), 40), s(b.get("display_name"), 40), s(b.get("auth_key"), 128)
    if not LOGIN_RE.fullmatch(login):
        raise ApiError(400, "bad_login", "Логин: 3–32 символа, латинские буквы, цифры, «_» и «.»")
    if not name:
        raise ApiError(400, "bad_name", "Введите отображаемое имя")
    if len(key) != 64 or not re.fullmatch(r"[0-9a-f]{64}", key):
        raise ApiError(400, "bad_key", "Некорректный ключ входа")
    if not captcha.consume(app.cfg, s(b.get("captcha_id"), 64), b.get("captcha_x")):
        raise ApiError(400, "captcha_failed", "Капча не пройдена")
    dev = b.get("device") if isinstance(b.get("device"), dict) else {}
    fp = s(dev.get("fingerprint"), 128)
    if name_taken(app, login):
        raise ApiError(409, "login_taken", "Этот логин уже занят")
    if fp and app.cfg.get("security", "one_account_per_device", True) and app.db.one("select 1 from users where fingerprint=?", (fp,)):
        raise ApiError(403, "device_registered", "С этого устройства уже создан аккаунт")
    salt = secrets.token_bytes(16)
    uid = app.db.x("insert into users(login,username,name,auth,salt,created,fingerprint,device) values(?,?,?,?,?,?,?,?)",
                   (login, login, name, hash_key(key, salt), salt, now(), fp, json.dumps(dev)[:4000]))
    u = app.db.one("select * from users where id=?", (uid,))
    return new_session(app, u, s(dev.get("os"), 80), c)


@route("POST", "/api/login", auth=False)
def login(app, c):
    lg, key = s(c.body.get("login"), 40), s(c.body.get("auth_key"), 128)
    rate(app, ("login", lg.lower()), int(app.cfg.get("security", "login_fail_limit", 10)), 600)
    u = app.db.one("select * from users where login=? and deleted=0 and is_bot=0", (lg,))
    if not u or not hmac.compare_digest(u["auth"], hash_key(key, u["salt"])):
        raise ApiError(401, "bad_credentials", "Неверный логин или пароль")
    if u["banned"]:
        raise ApiError(403, "banned", "Аккаунт заблокирован: " + (u["ban_reason"] or "нарушение правил"))
    if u["totp_secret"]:
        import ext_profile
        if not ext_profile.totp_ok(u["totp_secret"], s(c.body.get("totp"), 8)):
            raise ApiError(401, "2fa_required", "Введите код двухфакторной аутентификации")
    return new_session(app, u, "", c)


@route("GET", "/api/me")
def me(app, c):
    return {"user": pub_user(c.u), "me": me_dict(c.u), "premium": is_premium(c.u), "limits": {k: lim(app, c.u, k) for k in DEFAULT_LIMITS["free"]}}


@route("POST", "/api/keys/publish")
def keys_publish(app, c):
    pub = s(c.body.get("pub"), 100)
    if len(pub) < 40:
        raise ApiError(400, "bad_key", "Некорректный публичный ключ")
    app.db.x("update users set pub=? where id=?", (pub, c.u["id"]))
    return {"ok": True}


@route("GET", "/api/keys/get")
def keys_get(app, c):
    u = user_by_login(app, c.q.get("login"))
    return {"login": u["login"], "pub": u["pub"]}


@route("GET", "/api/search")
def search(app, c):
    q = s(c.q.get("q"), 40).lstrip("@")
    if len(q) < 2:
        return {"users": [], "chats": []}
    like = "%" + q.replace("%", "").replace("_", "") + "%"
    users = app.db.q("select username login,name,avatar_id,is_bot from users where deleted=0 and banned=0 and (username like ? or login like ? or name like ?) limit 20", (like, like, like))
    chats = app.db.q("select id,kind,title,username from chats where public=1 and banned=0 and kind in ('group','channel') and "
                     "(username like ? or title like ?) limit 20", (like, like))
    out = []
    for ch in chats:
        n = app.db.one("select count(*) n from members where chat_id=?", (ch["id"],))["n"]
        out.append({"id": ch["id"], "kind": ch["kind"], "title": ch["title"], "username": ch["username"] or "", "members": n})
    return {"users": [{"login": u["login"], "username": u["login"], "name": u["name"], "avatar_id": u["avatar_id"], "is_bot": bool(u["is_bot"])} for u in users], "chats": out}


# ------------------------------------------------------------------ chats
@route("POST", "/api/chats/leave")
def chats_leave(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if not m or m["role"] == "owner" or ch["kind"] == "private":
        raise ApiError(400, "cant_leave", "Нельзя покинуть этот чат")
    app.db.x("delete from members where chat_id=? and user_id=?", (ch["id"], c.u["id"]))
    return {"ok": True}


@route("POST", "/api/report")
def report(app, c):
    ch, m = get_member(app, int(c.body.get("chat_id") or 0), c.u["id"])
    if not (m or ch["public"]):
        raise ApiError(403, "forbidden", "Нет доступа")
    app.db.x("insert or replace into reports(chat_id,reporter_id,msg_id,reason,ts) values(?,?,?,?,?)",
             (ch["id"], c.u["id"], int(c.body.get("msg_id") or 0), s(c.body.get("reason"), 300), now()))
    return {"ok": True}


PAY_PAGE = """<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MINI max — оплата</title><style>body{font-family:Segoe UI,sans-serif;background:#17212b;color:#f5f5f5;text-align:center;margin-top:12vh}
button{background:#5288c1;color:#fff;border:0;border-radius:8px;padding:12px 28px;font-size:16px;cursor:pointer}</style></head><body>
<h2>Тестовый платёжный шлюз</h2><p>__DESC__</p><p><b>__SUM__</b></p><button onclick="pay()">Оплатить (тест)</button><p id="s"></p>
<script>function pay(){fetch('/pay/__ID__/confirm',{method:'POST'}).then(r=>r.json()).then(j=>{document.getElementById('s').textContent=
j.ok?'Оплачено! Вернитесь в MINI max.':'Ошибка: '+j.message})}</script></body></html>"""


def pay_page(app, pid):
    p = app.db.one("select * from payments where id=?", (pid,))
    if not p:
        return None
    desc = {"stars": "Звёзды: %s", "premium": "Premium: %s", "business": "MINI max для бизнеса"}.get(p["kind"], "%s")
    desc = desc % p["item"] if "%s" in desc else desc
    return (PAY_PAGE.replace("__DESC__", desc).replace("__SUM__", "%.2f %s" % (p["amount_minor"] / 100, p["currency"])).replace("__ID__", pid)
            + ("" if p["status"] == "pending" else "<!-- already paid -->"))


def pay_confirm(app, pid):
    with app.db.lock:
        p = app.db.one("select * from payments where id=?", (pid,))
        if not p or p["status"] != "pending":
            raise ApiError(400, "bad_payment", "Платёж не найден или уже оплачен")
        apply_payment(app, p)
    return {"ok": True}


# ------------------------------------------------------------------ HTTP handler
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    app = None

    def log_message(self, fmt, *a):
        log.debug(fmt % a)

    def do_GET(self):
        self._go("GET")

    def do_POST(self):
        self._go("POST")

    def do_PUT(self):
        self._go("PUT")

    def do_DELETE(self):
        self._go("DELETE")

    def _raw(self, code, data, ctype, headers=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.send_header("Cache-Control", "no-store")
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)
        self.close_connection = True

    def _json(self, code, obj):
        self._raw(code, json.dumps(obj, ensure_ascii=False).encode(), "application/json; charset=utf-8")

    def read_json(self, limit=None):
        n = int(self.headers.get("Content-Length") or 0)
        if n > (limit or int(self.app.cfg.get("server", "max_body_bytes", 4194304))):
            raise ApiError(413, "too_large", "Слишком большой запрос")
        raw = self.rfile.read(n) if n else b""
        try:
            b = json.loads(raw or b"{}")
        except ValueError:
            b = {}
        return b if isinstance(b, dict) else {}

    def drain(self, n):
        """Refuse an oversized upload politely: swallow a bounded amount of the body, then close the connection."""
        left = min(n, 16 << 20)
        while left > 0:
            b = self.rfile.read(min(1 << 20, left))
            if not b:
                break
            left -= len(b)
        self.close_connection = True

    def send_file(self, path, mime, name="", size=None, extra=None):
        """File streaming with HTTP Range support (video seeking, resumable downloads)."""
        size = size if size is not None else os.path.getsize(path)
        start, end, code = 0, size - 1, 200
        rg = self.headers.get("Range", "")
        m = re.fullmatch(r"bytes=(\d*)-(\d*)", rg.strip()) if rg else None
        if m and (m.group(1) or m.group(2)):
            if m.group(1):
                start = int(m.group(1))
                end = int(m.group(2)) if m.group(2) else size - 1
            else:
                start, end = max(0, size - int(m.group(2))), size - 1
            if start > end or start >= size:
                return self._raw(416, b"", "text/plain", {"Content-Range": "bytes */%d" % size})
            end, code = min(end, size - 1), 206
        self.send_response(code)
        self.send_header("Content-Type", mime or "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Connection", "close")
        self.send_header("Cache-Control", "no-store")
        if code == 206:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
        if name:
            self.send_header("Content-Disposition", "inline; filename*=UTF-8''" + __import__("urllib.parse").parse.quote(name))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command == "HEAD":
            return
        with open(path, "rb") as f:
            f.seek(start)
            left = end - start + 1
            while left > 0:
                chunk = f.read(min(1 << 20, left))
                if not chunk:
                    break
                self.wfile.write(chunk)
                left -= len(chunk)
        self.close_connection = True

    def _auth(self, app, c, token=None):
        h = self.headers.get("Authorization", "")
        tok = token or (h[7:].strip() if h.startswith("Bearer ") else "")
        sess = app.db.one("select * from sessions where token=?", (tok,)) if tok else None
        u = app.db.one("select * from users where id=? and deleted=0", (sess["user_id"],)) if sess else None
        if not u:
            raise ApiError(401, "unauthorized", "Требуется вход")
        if u["banned"]:
            raise ApiError(403, "banned", "Аккаунт заблокирован: " + (u["ban_reason"] or "нарушение правил"))
        if now() - sess["last"] > 30:
            app.db.x("update sessions set last=? where token=?", (now(), sess["token"]))
            app.db.x("update users set last_seen=? where id=?", (now(), u["id"]))
        c.u, c.s = u, sess

    def _go(self, method):
        app = self.app
        try:
            u = urlparse(self.path)
            path = u.path
            c = Ctx()
            c.q = {k: v[0] for k, v in parse_qs(u.query).items()}
            c.host = self.headers.get("Host", "localhost")
            c.ip = (self.headers.get("X-Forwarded-For", "").split(",")[0].strip() or self.client_address[0])
            c.body = {}
            if app.maintenance and path != "/api/ping":
                raise ApiError(503, "maintenance", "Технические работы, попробуйте позже")
            # --- HTML / static pages
            if method == "GET" and path.startswith("/captcha/"):
                cid = unquote(path[9:])
                if cid in captcha._store and captcha._store[cid]["mode"] == "hcaptcha":
                    return self._raw(200, captcha.page(app.cfg, cid).encode(), "text/html; charset=utf-8")
                return self._raw(404, "Ссылка устарела. Вернитесь в MINI max и повторите.".encode(), "text/plain; charset=utf-8")
            if method == "GET" and path.startswith("/pay/"):
                html = pay_page(app, unquote(path[5:]))
                return self._raw(200 if html else 404, (html or "Платёж не найден").encode(), "text/html; charset=utf-8")
            if method == "POST" and path.startswith("/pay/") and path.endswith("/confirm"):
                self.read_json()
                return self._json(200, pay_confirm(app, unquote(path[5:-8])))
            if method == "GET" and path.startswith("/update/"):
                base = app.cfg.path_of("storage", "updates_dir", "data/updates").resolve()
                f = (base / unquote(path[8:])).resolve()
                if base in f.parents and f.is_file():
                    return self.send_file(str(f), mimetypes.guess_type(f.name)[0] or "application/octet-stream")
                raise ApiError(404, "not_found", "unknown endpoint")
            # --- raw routes (uploads, downloads, Bot API)
            for m, pref, fn in RAW:
                if method == m and path.startswith(pref):
                    return fn(self, app, c, path)
            # --- JSON API
            if method == "POST" or method == "PUT":
                c.body = self.read_json()
            ent = ROUTES.get((method if method != "PUT" else "POST", path))
            if not ent:
                raise ApiError(404, "not_found", "unknown endpoint")
            fn, need_auth = ent
            if need_auth:
                self._auth(app, c)
            self._json(200, fn(app, c))
        except ApiError as e:
            self._json(e.code, {"error": e.error, "message": e.message})
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception:  # never leak internals to clients
            log.exception("unhandled error on %s %s", method, self.path)
            try:
                self._json(500, {"error": "server_error", "message": "Внутренняя ошибка сервера"})
            except Exception:
                pass


import ext_profile, ext_media, ext_msgs, ext_stars, ext_calls, ext_secret, ext_bots, ext_stories  # noqa: E402,F401  (register routes)
