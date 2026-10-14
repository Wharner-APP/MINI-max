#!/usr/bin/env python3
import argparse, logging, sys, threading, time
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import api, console
from config import Cfg
from db import DB


class App:
    pass


def build_app(cfg_path):
    app = App()
    app.cfg = Cfg(cfg_path)
    app.catalog = app.cfg.catalog()
    dbp = app.cfg.path_of("storage", "db_path", "data/minimax.db")
    dbp.parent.mkdir(parents=True, exist_ok=True)
    app.db = DB(dbp)
    app.rl, app.maintenance, app.started = {}, False, time.time()
    app.eph, app.eph_lock, app.pending_2fa, app.bf_state, app.waiters, app.wh_next = {}, threading.Lock(), {}, {}, {}, {}
    app.max_msg = lambda: app.db.one("select coalesce(max(id),0) n from messages")["n"]
    import ext_bots
    ext_bots.ensure_botfather(app)
    handler = type("H", (api.Handler,), {"app": app})
    app.httpd = ThreadingHTTPServer((app.cfg.get("server", "host", "0.0.0.0"), int(app.cfg.get("server", "port", 8080))), handler)
    app.httpd.daemon_threads = True
    return app


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=str(Path(__file__).parent / "configs" / "server.toml"))
    ap.add_argument("--no-console", action="store_true")
    a = ap.parse_args()
    app = build_app(a.config)
    lf = app.cfg.path_of("storage", "log_file", "data/server.log")
    lf.parent.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s %(message)s", handlers=[logging.FileHandler(lf, encoding="utf-8")])
    import captcha
    threading.Thread(target=app.httpd.serve_forever, daemon=True).start()
    import jobs
    jobs.start(app)
    host, port = app.httpd.server_address[:2]
    print("MINI max server: http://%s:%s | captcha: %s | БД: %s" % (host, port, captcha.mode(app.cfg), app.cfg.get("storage", "db_path")))
    if captcha.mode(app.cfg) != "hcaptcha":
        print("ВНИМАНИЕ: hCaptcha не активна (нет secret) — используется встроенный пазл.")
    if a.no_console:
        while True:
            time.sleep(3600)
    console.run(app)
    app.httpd.shutdown()


if __name__ == "__main__":
    main()
