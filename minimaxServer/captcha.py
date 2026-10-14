"""Captcha: hCaptcha (browser page served by this server, verified with the hCaptcha secret) or a built-in puzzle."""
import base64, io, json, os, random, secrets, threading, time, urllib.parse, urllib.request

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:  # pragma: no cover
    Image = None

W, H, PW = 312, 156, 52
_store, _lock = {}, threading.Lock()


def secret(cfg):
    return os.environ.get("MM_HCAPTCHA_SECRET") or cfg.get("captcha", "hcaptcha_secret", "")


def mode(cfg):
    m = cfg.get("captcha", "mode", "auto")
    if m == "auto":
        if cfg.get("captcha", "hcaptcha_sitekey") and secret(cfg):
            return "hcaptcha"
        return "puzzle" if Image else "unavailable"
    return m


def _gc():
    now = time.time()
    for k in [k for k, v in _store.items() if v["exp"] < now]:
        _store.pop(k, None)


def _uri(img):
    b = io.BytesIO()
    img.save(b, "PNG")
    return "data:image/png;base64," + base64.b64encode(b.getvalue()).decode()


def _puzzle():
    rnd = random.SystemRandom()
    bg = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(bg)
    c1 = tuple(rnd.randint(40, 200) for _ in range(3))
    c2 = tuple(rnd.randint(40, 200) for _ in range(3))
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=tuple(int(c1[i] * (1 - t) + c2[i] * t) for i in range(3)))
    for _ in range(40):
        x, y, r = rnd.randint(0, W), rnd.randint(0, H), rnd.randint(8, 30)
        d.ellipse([x - r, y - r, x + r, y + r], outline=tuple(rnd.randint(0, 255) for _ in range(3)), width=2)
    x0, y0 = rnd.randint(PW + 20, W - PW - 10), rnd.randint(10, H - PW - 10)
    mask = Image.new("L", (PW, PW), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle([6, 6, PW - 6, PW - 6], radius=8, fill=255)
    md.ellipse([PW // 2 - 8, 0, PW // 2 + 8, 16], fill=255)
    piece = Image.new("RGBA", (PW, PW), (0, 0, 0, 0))
    piece.paste(bg.crop((x0, y0, x0 + PW, y0 + PW)).convert("RGBA"), (0, 0), mask)
    out = Image.new("RGBA", (PW, PW), (0, 0, 0, 0))
    out.alpha_composite(piece.filter(ImageFilter.GaussianBlur(1)))
    out.alpha_composite(piece)
    bg = bg.convert("RGBA")
    bg.paste(Image.new("RGBA", (PW, PW), (0, 0, 0, 110)), (x0, y0), mask)
    return bg, out, x0, y0


def create(cfg):
    m, ttl = mode(cfg), int(cfg.get("captcha", "ttl_seconds", 180))
    cid = secrets.token_hex(8)
    with _lock:
        _gc()
        if m == "hcaptcha":
            _store[cid] = {"mode": m, "solved": False, "exp": time.time() + ttl}
            return {"mode": "hcaptcha", "captcha_id": cid, "url": "/captcha/" + cid, "expires_in": ttl}
        if m == "puzzle" and Image:
            bg, piece, x0, y0 = _puzzle()
            _store[cid] = {"mode": m, "x": x0, "exp": time.time() + ttl}
            return {"mode": "puzzle", "captcha_id": cid, "background": _uri(bg), "piece": _uri(piece), "piece_y": y0,
                    "width": W, "height": H, "expires_in": ttl}
    return None


def status(cid):
    c = _store.get(cid)
    if not c or c["exp"] < time.time():
        return {"solved": False, "expired": True}
    return {"solved": bool(c.get("solved")), "expired": False}


def consume(cfg, cid, x):
    if mode(cfg) == "off":
        return True
    with _lock:
        c = _store.get(cid)
        if not c or c["exp"] < time.time():
            _store.pop(cid, None)
            return False
        if c["mode"] == "hcaptcha":
            if not c.get("solved"):
                return False
            _store.pop(cid, None)
            return True
        _store.pop(cid, None)
        try:
            return abs(int(x) - c["x"]) <= 6
        except (TypeError, ValueError):
            return False


def verify_hcaptcha(cfg, cid, token, ip=""):
    c = _store.get(cid)
    if not c or c["mode"] != "hcaptcha" or c["exp"] < time.time():
        return False, "expired"
    data = urllib.parse.urlencode({"secret": secret(cfg), "response": token,
                                   "sitekey": cfg.get("captcha", "hcaptcha_sitekey", "")}).encode()
    try:
        with urllib.request.urlopen(urllib.request.Request("https://api.hcaptcha.com/siteverify", data=data), timeout=10) as r:
            res = json.loads(r.read())
    except Exception as e:  # network problem
        return False, "hCaptcha unreachable: %s" % e
    if res.get("success"):
        c["solved"] = True
        return True, ""
    return False, ",".join(res.get("error-codes", [])) or "failed"


PAGE = """<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MINI max — проверка</title><script src="https://js.hcaptcha.com/1/api.js" async defer></script>
<style>body{font-family:Segoe UI,sans-serif;background:#17212b;color:#f5f5f5;display:flex;flex-direction:column;align-items:center;margin-top:12vh}
#s{margin-top:18px;color:#6ab3f3}</style></head><body><h2>Подтвердите, что вы не робот</h2>
<div class="h-captcha" data-sitekey="__KEY__" data-callback="ok"></div><div id="s"></div>
<script>function ok(t){fetch('/api/captcha/verify',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({captcha_id:'__ID__',token:t})}).then(r=>r.json()).then(j=>{document.getElementById('s').textContent=
j.ok?'Готово! Вернитесь в окно MINI max.':'Ошибка: '+(j.message||'')})}</script></body></html>"""


def page(cfg, cid):
    return PAGE.replace("__KEY__", cfg.get("captcha", "hcaptcha_sitekey", "")).replace("__ID__", cid)
