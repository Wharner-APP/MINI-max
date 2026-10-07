#!/usr/bin/env python3
"""Minimal reference/mock of the MINI max server API (development only, in-memory, no real security).

  python3 tools/mock_server.py --port 8080 [--updates DIR]      # DIR/<platformId>/{manifest.json,manifest.sig,files/...}

Implements: /api/ping, /api/get_captcha, /api/device/check, /api/register, /api/login,
/api/profile/update, /api/sessions/terminate_others and the /update/... static tree.
See docs/api.md for the contract the real server must follow.
"""
import argparse, base64, hashlib, io, json, random, secrets, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse

from PIL import Image, ImageDraw, ImageFilter

W, H, PW = 312, 156, 52
captchas, users, devices = {}, {}, set()
UPDATES = None


def make_captcha():
    rnd = random.Random()
    bg = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(bg)
    c1 = tuple(rnd.randint(40, 200) for _ in range(3)); c2 = tuple(rnd.randint(40, 200) for _ in range(3))
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
    glow = piece.filter(ImageFilter.GaussianBlur(1))
    out = Image.new("RGBA", (PW, PW), (0, 0, 0, 0)); out.alpha_composite(glow); out.alpha_composite(piece)
    hole = Image.new("RGBA", (PW, PW), (0, 0, 0, 110))
    bg = bg.convert("RGBA"); bg.paste(hole, (x0, y0), mask)

    def uri(img):
        b = io.BytesIO(); img.save(b, "PNG"); return "data:image/png;base64," + base64.b64encode(b.getvalue()).decode()
    cid = secrets.token_hex(8)
    captchas[cid] = x0
    return {"captcha_id": cid, "background": uri(bg), "piece": uri(out), "piece_y": y0, "width": W, "height": H, "expires_in": 120}


class H_(BaseHTTPRequestHandler):
    def log_message(self, fmt, *a):
        sys.stderr.write("[mock] " + fmt % a + "\n")

    def send_json(self, obj, code=200):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(n) or b"{}")
        except Exception:
            return {}

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/ping":
            return self.send_json({"ok": True, "server": "MINI max (mock)", "version": "0"})
        if path == "/api/get_captcha":
            return self.send_json(make_captcha())
        if path.startswith("/update/") and UPDATES:
            f = (Path(UPDATES) / unquote(path[len("/update/"):])).resolve()
            if str(f).startswith(str(Path(UPDATES).resolve())) and f.is_file():
                b = f.read_bytes()
                self.send_response(200); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
                return
        self.send_json({"error": "not_found", "message": "unknown endpoint"}, 404)

    def do_POST(self):
        path = urlparse(self.path).path
        b = self.body()
        if path == "/api/device/check":
            return self.send_json({"registered": b.get("fingerprint") in devices})
        if path == "/api/register":
            x = captchas.pop(b.get("captcha_id", ""), None)
            if x is None or abs(int(b.get("captcha_x", -999)) - x) > 6:
                return self.send_json({"error": "captcha_failed", "message": "Капча не пройдена"}, 400)
            login = b.get("login", "").lower()
            if login in users:
                return self.send_json({"error": "login_taken", "message": "Этот логин уже занят"}, 409)
            if b.get("device", {}).get("fingerprint") in devices:
                return self.send_json({"error": "device_registered", "message": "С этого устройства уже создан аккаунт"}, 403)
            users[login] = {"auth": hashlib.sha256(b["auth_key"].encode()).hexdigest(), "name": b["display_name"], "id": len(users) + 1}
            devices.add(b.get("device", {}).get("fingerprint"))
            return self.send_json({"token": secrets.token_hex(16), "user": {"id": users[login]["id"], "login": login, "display_name": b["display_name"], "bio": ""}})
        if path == "/api/login":
            u = users.get(b.get("login", "").lower())
            if not u or u["auth"] != hashlib.sha256(b.get("auth_key", "").encode()).hexdigest():
                return self.send_json({"error": "bad_credentials", "message": "Неверный логин или пароль"}, 401)
            return self.send_json({"token": secrets.token_hex(16), "user": {"id": u["id"], "login": b["login"].lower(), "display_name": u["name"], "bio": ""}})
        if path in ("/api/profile/update", "/api/sessions/terminate_others"):
            return self.send_json({"ok": True})
        self.send_json({"error": "not_found", "message": "unknown endpoint"}, 404)


def main():
    global UPDATES
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--updates")
    a = ap.parse_args()
    UPDATES = a.updates
    print(f"mock server on http://{a.host}:{a.port}")
    ThreadingHTTPServer((a.host, a.port), H_).serve_forever()


if __name__ == "__main__":
    main()
