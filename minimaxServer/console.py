"""Admin console. Privacy policy enforced here:
  * `read`/`find-msg`/`members` work ONLY for open chats (public groups/channels);
  * private chats are never readable (their content is end-to-end encrypted anyway);
  * a private chat can be banned/deleted only when it has MORE than [moderation].report_ban_threshold reports.
"""
import json, logging, os, secrets, shlex, shutil, time
from pathlib import Path

import api, captcha, manifest

CMDS = {}


def cmd(name, usage, desc):
    def deco(f):
        CMDS[name] = (f, usage, desc)
        return f
    return deco


def ts(t):
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t)) if t else "-"


def log_admin(app, line):
    app.db.x("insert into admin_log(ts,line) values(?,?)", (int(time.time()), line))
    logging.getLogger("mm.admin").info(line)


def table(rows, head):
    rows = [[str(x) for x in r] for r in rows]
    w = [max(len(h), *(len(r[i]) for r in rows)) if rows else len(h) for i, h in enumerate(head)]
    print("  ".join(h.ljust(w[i]) for i, h in enumerate(head)))
    for r in rows:
        print("  ".join(r[i].ljust(w[i]) for i in range(len(head))))
    print("(%d)" % len(rows))


def need_user(app, login):
    u = app.db.one("select * from users where username=? or login=?", (login.lstrip("@"), login.lstrip("@")))
    if not u:
        raise ValueError("пользователь не найден: " + login)
    return u


def need_chat(app, ref):
    c = app.db.one("select * from chats where username=?", (ref[1:],)) if ref.startswith("@") else \
        (app.db.one("select * from chats where id=?", (int(ref),)) if ref.isdigit() else None)
    if not c:
        raise ValueError("чат не найден: " + ref)
    return c


def is_open(c):
    return bool(c["public"]) and c["kind"] in ("group", "channel") and not c["dm_key"]


def reports_n(app, cid):
    return app.db.one("select count(*) n from reports where chat_id=?", (cid,))["n"]


def may_moderate(app, c):
    thr = int(app.cfg.get("moderation", "report_ban_threshold", 10))
    if is_open(c):
        return True
    if reports_n(app, c["id"]) > thr:
        return True
    raise PermissionError("приватный чат: модерация возможна только при более чем %d жалобах (сейчас %d)" % (thr, reports_n(app, c["id"])))


def need_open(c):
    if not is_open(c):
        raise PermissionError("чат приватный — чтение запрещено политикой конфиденциальности")


# ------------------------------------------------------------------ general
@cmd("help", "help [команда]", "список команд")
def _help(app, a):
    if a and a[0] in CMDS:
        print(CMDS[a[0]][1], "—", CMDS[a[0]][2])
        return
    for k in sorted(CMDS):
        print("  %-14s %-34s %s" % (k, CMDS[k][1], CMDS[k][2]))


@cmd("stats", "stats", "общая статистика")
def _stats(app, a):
    d = app.db
    one = lambda q: d.one(q)["n"]
    print("пользователей:", one("select count(*) n from users where deleted=0"), "| забанено:", one("select count(*) n from users where banned=1"),
          "| онлайн (5 мин):", one("select count(*) n from users where last_seen>%d" % (time.time() - 300)))
    print("чатов: личных %d, групп %d, каналов %d" % tuple(one("select count(*) n from chats where kind='%s'" % k) for k in ("private", "group", "channel")))
    print("сообщений:", one("select count(*) n from messages"), "| жалоб:", one("select count(*) n from reports"), "| платежей оплачено:",
          one("select count(*) n from payments where status='paid'"))


@cmd("uptime", "uptime", "время работы")
def _uptime(app, a):
    print("работает %d мин" % ((time.time() - app.started) // 60))


@cmd("online", "online", "пользователи, активные за 5 минут")
def _online(app, a):
    table([(r["login"], r["name"], ts(r["last_seen"])) for r in app.db.q("select * from users where last_seen>? order by last_seen desc", (time.time() - 300,))],
          ["login", "имя", "активность"])


@cmd("config", "config", "показать конфигурацию (без секретов)")
def _config(app, a):
    d = json.loads(json.dumps(app.cfg.d))
    d.get("captcha", {}).pop("hcaptcha_secret", None)
    print(json.dumps(d, indent=1, ensure_ascii=False))
    print("режим капчи:", captcha.mode(app.cfg))


@cmd("captcha", "captcha", "режим капчи")
def _captcha(app, a):
    m = captcha.mode(app.cfg)
    print("режим:", m)
    if m != "hcaptcha":
        print("hCaptcha не активна: задайте secret (env MM_HCAPTCHA_SECRET или [captcha].hcaptcha_secret)")


@cmd("maint", "maint on|off", "режим техработ")
def _maint(app, a):
    app.maintenance = bool(a) and a[0] == "on"
    print("техработы:", "ВКЛ" if app.maintenance else "выкл")


@cmd("loglevel", "loglevel debug|info|warning", "уровень логов")
def _loglevel(app, a):
    logging.getLogger().setLevel(a[0].upper())


@cmd("adminlog", "adminlog [n]", "журнал действий администратора")
def _adminlog(app, a):
    for r in reversed(app.db.q("select * from admin_log order by id desc limit ?", (int(a[0]) if a else 20,))):
        print(ts(r["ts"]), r["line"])


@cmd("backup", "backup", "копия базы данных")
def _backup(app, a):
    dst = app.cfg.path_of("storage", "db_path", "data/minimax.db").parent / ("backup-%s.db" % time.strftime("%Y%m%d-%H%M%S"))
    with app.db.lock:
        import sqlite3
        b = sqlite3.connect(str(dst))
        app.db.c.backup(b)
        b.close()
    print("готово:", dst)


@cmd("vacuum", "vacuum", "сжать базу")
def _vacuum(app, a):
    app.db.x("vacuum")
    print("ok")


@cmd("clear", "clear", "очистить экран")
def _clear(app, a):
    os.system("cls" if os.name == "nt" else "clear")


@cmd("exit", "exit", "остановить сервер")
def _exit(app, a):
    raise SystemExit


CMDS["quit"] = CMDS["stop"] = CMDS["exit"]


# ------------------------------------------------------------------ users
@cmd("users", "users [фильтр] [лимит]", "список пользователей")
def _users(app, a):
    like = "%" + (a[0] if a else "") + "%"
    table([(r["id"], r["login"], r["name"], "BAN" if r["banned"] else "", r["stars"], "P" if r["premium_until"] > time.time() else "", ts(r["last_seen"]))
           for r in app.db.q("select * from users where deleted=0 and (login like ? or name like ?) order by id desc limit ?", (like, like, int(a[1]) if len(a) > 1 else 50))],
          ["id", "login", "имя", "бан", "звёзд", "prem", "активность"])


@cmd("user", "user <login>", "карточка пользователя")
def _user(app, a):
    u = need_user(app, a[0])
    print("id %d | @%s | %s | регистрация %s | активность %s" % (u["id"], u["login"], u["name"], ts(u["created"]), ts(u["last_seen"])))
    print("bio: %s | бан: %s %s" % (u["bio"], bool(u["banned"]), u["ban_reason"]))
    print("звёзды: %d | premium до %s | business до %s" % (u["stars"], ts(u["premium_until"]), ts(u["business_until"])))
    dev = json.loads(u["device"] or "{}")
    print("устройство (указано при регистрации): %s | %s" % (dev.get("os", "-"), dev.get("hostname", "-")))
    print("сессий:", app.db.one("select count(*) n from sessions where user_id=?", (u["id"],))["n"],
          "| подарков:", app.db.one("select count(*) n from gifts where user_id=?", (u["id"],))["n"])


@cmd("find", "find <текст>", "поиск пользователей")
def _find(app, a):
    _users(app, a)


@cmd("ban", "ban <login> [причина]", "забанить аккаунт (сессии завершаются)")
def _ban(app, a):
    u = need_user(app, a[0])
    app.db.x("update users set banned=1, ban_reason=? where id=?", (" ".join(a[1:]), u["id"]))
    app.db.x("delete from sessions where user_id=?", (u["id"],))
    log_admin(app, "ban user %s: %s" % (u["login"], " ".join(a[1:])))
    print("забанен", u["login"])


@cmd("unban", "unban <login>", "разбанить")
def _unban(app, a):
    u = need_user(app, a[0])
    app.db.x("update users set banned=0, ban_reason='' where id=?", (u["id"],))
    log_admin(app, "unban user " + u["login"])


@cmd("kick", "kick <login>", "завершить все сессии")
def _kick(app, a):
    u = need_user(app, a[0])
    app.db.x("delete from sessions where user_id=?", (u["id"],))
    log_admin(app, "kick " + u["login"])


@cmd("sessions", "sessions <login>", "сессии пользователя")
def _sessions(app, a):
    u = need_user(app, a[0])
    table([(r["device"], ts(r["created"]), ts(r["last"])) for r in app.db.q("select * from sessions where user_id=?", (u["id"],))], ["устройство", "создана", "активность"])


@cmd("setname", "setname <login> <имя>", "сменить имя")
def _setname(app, a):
    u = need_user(app, a[0])
    app.db.x("update users set name=? where id=?", (" ".join(a[1:])[:40], u["id"]))
    log_admin(app, "setname " + u["login"])


@cmd("clearbio", "clearbio <login>", "очистить «о себе»")
def _clearbio(app, a):
    app.db.x("update users set bio='' where id=?", (need_user(app, a[0])["id"],))


def anonymize(app, u):
    uid = u["id"]
    app.db.x("update users set deleted=1, login=?, username=?, name='Удалённый аккаунт', bio='', pub='', fingerprint=NULL, device='', avatar_id=0, totp_secret='', key_backup='', "
             "emoji_status='', birthday='', bot_token=NULL where id=?", ("deleted_%d" % uid, "deleted_%d" % uid, uid))
    for t in ("sessions", "contacts", "blocks", "user_settings", "drafts", "chat_prefs", "e2e_devices", "e2e_otk"):
        app.db.x("delete from %s where user_id=?" % t, (uid,))
    app.db.x("delete from contacts where contact_id=?", (uid,))
    app.db.x("delete from members where user_id=? and role!='owner'", (uid,))


@cmd("deluser", "deluser <login>", "удалить аккаунт (анонимизация, освобождает логин)")
def _deluser(app, a):
    u = need_user(app, a[0])
    anonymize(app, u)
    log_admin(app, "deluser " + u["login"])
    print("удалён")


# ------------------------------------------------------------------ stars / gifts / premium
@cmd("stars", "stars give|take|set <login> <n>", "управление звёздами")
def _stars(app, a):
    op, u, n = a[0], need_user(app, a[1]), int(a[2])
    q = {"give": "update users set stars=stars+? where id=?", "take": "update users set stars=max(0,stars-?) where id=?",
         "set": "update users set stars=? where id=?"}[op]
    app.db.x(q, (n, u["id"]))
    log_admin(app, "stars %s %s %d" % (op, u["login"], n))
    print("баланс:", app.db.one("select stars from users where id=?", (u["id"],))["stars"])


@cmd("balance", "balance <login>", "баланс звёзд")
def _balance(app, a):
    print(need_user(app, a[0])["stars"])


@cmd("gifts", "gifts [login]", "каталог подарков или подарки пользователя")
def _gifts(app, a):
    if not a:
        table([(g["id"], g["name"], g["stars"]) for g in app.catalog["gifts"]], ["id", "название", "звёзд"])
        return
    u = need_user(app, a[0])
    table([(r["id"], r["gift_id"], r["stars"], ts(r["ts"])) for r in app.db.q("select * from gifts where user_id=?", (u["id"],))], ["#", "подарок", "звёзд", "когда"])


@cmd("gift", "gift <login> <gift_id>", "выдать подарок бесплатно")
def _gift(app, a):
    u = need_user(app, a[0])
    g = next((x for x in app.catalog["gifts"] if x["id"] == a[1]), None)
    if not g:
        raise ValueError("нет такого подарка (см. gifts)")
    app.db.x("insert into gifts(user_id,from_id,gift_id,stars,ts) values(?,?,?,?,?)", (u["id"], 0, g["id"], g["stars"], int(time.time())))
    log_admin(app, "gift %s -> %s" % (g["id"], u["login"]))


@cmd("revokegift", "revokegift <#>", "забрать подарок")
def _revokegift(app, a):
    app.db.x("delete from gifts where id=?", (int(a[0]),))


def _until(app, col, a):
    op, u = a[0], need_user(app, a[1])
    if op == "give":
        days = int(a[2]) if len(a) > 2 else 30
        app.db.x("update users set %s=max(?,%s)+? where id=?" % (col, col), (int(time.time()), days * 86400, u["id"]))
    else:
        app.db.x("update users set %s=0 where id=?" % col, (u["id"],))
    log_admin(app, "%s %s %s" % (col, op, u["login"]))
    print("до", ts(app.db.one("select %s v from users where id=?" % col, (u["id"],))["v"]))


@cmd("premium", "premium give|revoke <login> [дней]", "Premium")
def _premium(app, a):
    _until(app, "premium_until", a)


@cmd("business", "business give|revoke <login> [дней]", "MINI max для бизнеса")
def _business(app, a):
    _until(app, "business_until", a)


@cmd("premiumlist", "premiumlist", "пользователи с Premium")
def _premiumlist(app, a):
    table([(r["login"], ts(r["premium_until"])) for r in app.db.q("select * from users where premium_until>?", (time.time(),))], ["login", "до"])


# ------------------------------------------------------------------ chats
@cmd("chats", "chats [group|channel|private] [open]", "список чатов (личные — только счётчик)")
def _chats(app, a):
    kind = a[0] if a and a[0] in ("group", "channel", "private") else None
    if kind == "private":
        print("личных чатов:", app.db.one("select count(*) n from chats where kind='private'")["n"], "(содержимое недоступно)")
        return
    rows = app.db.q("select * from chats where kind!='private' %s order by id desc limit 100" % ("and kind='%s'" % kind if kind else ""))
    table([(r["id"], r["kind"], "@" + r["username"] if r["username"] else "(приватный)", r["title"], app.db.one("select count(*) n from members where chat_id=?", (r["id"],))["n"],
            reports_n(app, r["id"]), "BAN" if r["banned"] else "") for r in rows], ["id", "тип", "ссылка", "название", "участн.", "жалоб", "бан"])


@cmd("chat", "chat <id|@username>", "информация о чате")
def _chat(app, a):
    c = need_chat(app, a[0])
    print("id %d | %s | %s | %s | создан %s" % (c["id"], c["kind"], c["title"] or "(личный)", "открытый" if is_open(c) else "приватный", ts(c["created"])))
    print("участников:", app.db.one("select count(*) n from members where chat_id=?", (c["id"],))["n"], "| сообщений:",
          app.db.one("select count(*) n from messages where chat_id=?", (c["id"],))["n"], "| жалоб:", reports_n(app, c["id"]),
          "| бан:", bool(c["banned"]), c["ban_reason"])


@cmd("read", "read <id|@username> [n]", "читать последние сообщения ОТКРЫТОГО чата")
def _read(app, a):
    c = need_chat(app, a[0])
    need_open(c)
    rows = app.db.q("select m.*,u.login from messages m join users u on u.id=m.sender_id where m.chat_id=? and m.deleted=0 order by m.id desc limit ?", (c["id"], int(a[1]) if len(a) > 1 else 30))
    for r in reversed(rows):
        print("[%s] #%d %s: %s" % (ts(r["ts"]), r["id"], r["login"], r["body"]))
    log_admin(app, "read open chat %d" % c["id"])


@cmd("find-msg", "find-msg <текст>", "поиск по сообщениям ОТКРЫТЫХ чатов")
def _find_msg(app, a):
    like = "%" + " ".join(a) + "%"
    rows = app.db.q("select m.*,u.login,c.username,c.id cid from messages m join chats c on c.id=m.chat_id join users u on u.id=m.sender_id "
                    "where c.public=1 and c.kind in ('group','channel') and m.deleted=0 and m.body like ? order by m.id desc limit 30", (like,))
    for r in rows:
        print("[%s] chat %d @%s #%d %s: %s" % (ts(r["ts"]), r["cid"], r["username"], r["id"], r["login"], r["body"][:120]))


@cmd("members", "members <id|@username>", "участники ОТКРЫТОГО чата")
def _members(app, a):
    c = need_chat(app, a[0])
    need_open(c)
    table([(r["login"], r["name"], r["role"]) for r in app.db.q("select u.login,u.name,m.role from members m join users u on u.id=m.user_id where m.chat_id=? limit 500", (c["id"],))], ["login", "имя", "роль"])


@cmd("chatban", "chatban <чат> [причина]", "заблокировать чат (приватный — только при >порога жалоб)")
def _chatban(app, a):
    c = need_chat(app, a[0])
    may_moderate(app, c)
    app.db.x("update chats set banned=1, ban_reason=? where id=?", (" ".join(a[1:]), c["id"]))
    log_admin(app, "chatban %d (reports=%d) %s" % (c["id"], reports_n(app, c["id"]), " ".join(a[1:])))
    print("чат заблокирован")


@cmd("chatunban", "chatunban <чат>", "снять блокировку чата")
def _chatunban(app, a):
    c = need_chat(app, a[0])
    app.db.x("update chats set banned=0, ban_reason='' where id=?", (c["id"],))
    log_admin(app, "chatunban %d" % c["id"])


@cmd("chatdel", "chatdel <чат>", "удалить чат (приватный — только при >порога жалоб)")
def _chatdel(app, a):
    c = need_chat(app, a[0])
    may_moderate(app, c)
    for t in ("messages", "members", "reports"):
        app.db.x("delete from %s where chat_id=?" % t, (c["id"],))
    app.db.x("delete from chats where id=?", (c["id"],))
    log_admin(app, "chatdel %d" % c["id"])
    print("удалён")


@cmd("chatrename", "chatrename <чат> <название>", "переименовать открытый чат")
def _chatrename(app, a):
    c = need_chat(app, a[0])
    need_open(c)
    app.db.x("update chats set title=? where id=?", (" ".join(a[1:])[:64], c["id"]))


@cmd("chatuser", "chatuser <чат> <username>", "сменить публичную ссылку открытого чата")
def _chatuser(app, a):
    c = need_chat(app, a[0])
    need_open(c)
    if not api.UNAME_RE.fullmatch(a[1]):
        raise ValueError("некорректный username")
    app.db.x("update chats set username=? where id=?", (a[1], c["id"]))


@cmd("chatadmin", "chatadmin <чат> <login> [off]", "назначить/снять администратора открытого чата")
def _chatadmin(app, a):
    c, u = need_chat(app, a[0]), need_user(app, a[1])
    need_open(c)
    app.db.x("update members set role=? where chat_id=? and user_id=? and role!='owner'", ("member" if len(a) > 2 else "admin", c["id"], u["id"]))


@cmd("chatkick", "chatkick <чат> <login>", "исключить участника из открытого чата")
def _chatkick(app, a):
    c, u = need_chat(app, a[0]), need_user(app, a[1])
    need_open(c)
    app.db.x("delete from members where chat_id=? and user_id=? and role!='owner'", (c["id"], u["id"]))
    log_admin(app, "chatkick %d %s" % (c["id"], u["login"]))


@cmd("msgdel", "msgdel <id>", "удалить сообщение в открытом чате")
def _msgdel(app, a):
    m = app.db.one("select * from messages where id=?", (int(a[0]),))
    if not m:
        raise ValueError("нет такого сообщения")
    need_open(app.db.one("select * from chats where id=?", (m["chat_id"],)))
    app.db.x("update messages set deleted=1 where id=?", (m["id"],))
    log_admin(app, "msgdel %d" % m["id"])


# ------------------------------------------------------------------ reports
@cmd("reports", "reports [мин]", "чаты с жалобами; ПОРОГ — чаты, которые можно блокировать")
def _reports(app, a):
    thr = int(app.cfg.get("moderation", "report_ban_threshold", 10))
    rows = app.db.q("select chat_id, count(*) n from reports group by chat_id having n>=? order by n desc", (int(a[0]) if a else 1,))
    out = []
    for r in rows:
        c = app.db.one("select * from chats where id=?", (r["chat_id"],))
        out.append((c["id"], c["kind"], "@" + c["username"] if c["username"] else "(приватный)", c["title"], r["n"], "МОЖНО БАНИТЬ" if is_open(c) or r["n"] > thr else "ниже порога"))
    table(out, ["id", "тип", "ссылка", "название", "жалоб", "статус"])


@cmd("report", "report <чат>", "причины жалоб на чат (текст жалоб — от пользователей)")
def _report(app, a):
    c = need_chat(app, a[0])
    for r in app.db.q("select reason, ts from reports where chat_id=? order by ts desc limit 50", (c["id"],)):
        print(ts(r["ts"]), r["reason"])
    print("всего жалоб:", reports_n(app, c["id"]))


@cmd("reportclear", "reportclear <чат>", "сбросить жалобы")
def _reportclear(app, a):
    c = need_chat(app, a[0])
    app.db.x("delete from reports where chat_id=?", (c["id"],))
    log_admin(app, "reportclear %d" % c["id"])


# ------------------------------------------------------------------ announcements, updates
def sysuser(app):
    u = app.db.one("select * from users where login='minimax_news'")
    if u:
        return u
    salt = secrets.token_bytes(16)
    uid = app.db.x("insert into users(login,name,auth,salt,created) values('minimax_news','Новости MINI max',?,?,?)", (secrets.token_hex(32), salt, int(time.time())))
    return app.db.one("select * from users where id=?", (uid,))


@cmd("announce", "announce <текст>", "опубликовать в канале @mm_news (все пользователи подписываются)")
def _announce(app, a):
    s = sysuser(app)
    c = app.db.one("select * from chats where username='mm_news'")
    if not c:
        cid = app.db.x("insert into chats(kind,title,username,about,public,owner_id,created) values('channel','Новости MINI max','mm_news','Официальный канал',1,?,?)", (s["id"], int(time.time())))
        app.db.x("insert into members(chat_id,user_id,role,joined) values(?,?,'owner',?)", (cid, s["id"], int(time.time())))
        c = app.db.one("select * from chats where id=?", (cid,))
    app.db.x("insert or ignore into members(chat_id,user_id,joined) select ?,id,? from users where deleted=0 and id!=?", (c["id"], int(time.time()), s["id"]))
    app.db.x("insert into messages(chat_id,sender_id,ts,body,enc) values(?,?,?,?,0)", (c["id"], s["id"], int(time.time()), " ".join(a)))
    api.notify()
    log_admin(app, "announce: " + " ".join(a))


@cmd("update-keys", "update-keys", "создать ключи подписи обновлений (keys/)")
def _ukeys(app, a):
    d = app.cfg.path_of("updates", "private_key", "keys/update_private.key").parent
    manifest.gen_keys(d)
    print("ключи в", d, "— update_public.key положите в configs/ клиента до сборки")


@cmd("update-publish", "update-publish <папка> <платформа>", "опубликовать обновление клиента (minimaxWinX64, ...)")
def _upub(app, a):
    rev, n = manifest.publish(Path(a[0]), a[1], app.cfg.path_of("storage", "updates_dir", "data/updates"),
                              app.cfg.path_of("updates", "private_key", "keys/update_private.key"))
    log_admin(app, "update-publish %s rev=%s" % (a[1], rev))
    print("опубликовано: revision %s, файлов %d" % (rev, n))


@cmd("update-list", "update-list", "опубликованные обновления")
def _ulist(app, a):
    root = app.cfg.path_of("storage", "updates_dir", "data/updates")
    for p in sorted(root.glob("*/manifest.json")) if root.exists() else []:
        m = json.loads(p.read_text())
        print(p.parent.name, m["revision"], m["created"])


# ------------------------------------------------------------------ bots, media, stickers, calls, devices
@cmd("bots", "bots [owner]", "список ботов")
def _bots(app, a):
    rows = app.db.q("select b.*, o.username owner from users b left join users o on o.id=b.bot_owner where b.is_bot=1 and b.deleted=0")
    table([(r["id"], "@" + r["username"], r["owner"] or "-", "inline" if r["bot_inline"] else "", "webapp" if r["webapp_url"] else "", ts(r["created"])) for r in rows], ["id", "бот", "владелец", "inline", "webapp", "создан"])


@cmd("botban", "botban <username> [причина]", "заблокировать бота")
def _botban(app, a):
    _ban(app, a)


@cmd("botdel", "botdel <username>", "удалить бота")
def _botdel(app, a):
    u = need_user(app, a[0])
    app.db.x("update users set deleted=1, bot_token=NULL, username=? where id=? and is_bot=1", ("deleted_%d" % u["id"], u["id"]))
    log_admin(app, "botdel " + a[0])


@cmd("mediastats", "mediastats", "статистика хранилища медиа")
def _mediastats(app, a):
    table([(r["kind"], r["n"], "%.1f МБ" % (r["sz"] / 1048576)) for r in app.db.q("select kind, count(*) n, coalesce(sum(size),0) sz from media where ready=1 group by kind")], ["тип", "файлов", "объём"])


@cmd("media", "media <id>", "сведения о файле (содержимое не показывается)")
def _media(app, a):
    m = app.db.one("select m.*, u.username owner from media m join users u on u.id=m.owner_id where m.id=?", (int(a[0]),))
    if not m:
        raise ValueError("нет такого файла")
    print("#%d %s %s %s %d байт, владелец @%s, зашифрован клиентом: %s, sha256 %s" % (m["id"], m["kind"], m["name"], m["mime"], m["size"], m["owner"], bool(m["enc"]), m["sha256"][:16]))


@cmd("mediadel", "mediadel <id>", "удалить файл (например, по жалобе на публичный пост)")
def _mediadel(app, a):
    m = app.db.one("select * from media where id=?", (int(a[0]),))
    if m:
        app.db.x("delete from media where id=?", (m["id"],))
        app.db.x("update messages set media_id=0 where media_id=?", (m["id"],))
        Path(m["path"]).unlink(missing_ok=True)
        log_admin(app, "mediadel %d" % m["id"])


@cmd("packs", "packs", "наборы стикеров")
def _packs(app, a):
    table([(r["id"], r["short_name"], r["title"], "premium" if r["premium"] else "", "official" if r["official"] else "") for r in app.db.q("select * from sticker_packs")], ["id", "имя", "название", "premium", "official"])


@cmd("packofficial", "packofficial <имя> [off]", "пометить набор официальным")
def _packofficial(app, a):
    app.db.x("update sticker_packs set official=? where short_name=?", (0 if len(a) > 1 else 1, a[0]))


@cmd("packdel", "packdel <имя>", "удалить набор стикеров")
def _packdel(app, a):
    p = app.db.one("select * from sticker_packs where short_name=?", (a[0],))
    if p:
        app.db.x("delete from stickers where pack_id=?", (p["id"],))
        app.db.x("delete from pack_installs where pack_id=?", (p["id"],))
        app.db.x("delete from sticker_packs where id=?", (p["id"],))


@cmd("gifadd", "gifadd <media_id> <теги...>", "добавить GIF (media kind=gif) в общую библиотеку")
def _gifadd(app, a):
    app.db.x("insert or replace into gif_library(media_id,tags,ts) values(?,?,?)", (int(a[0]), " ".join(a[1:]), int(time.time())))


@cmd("gifdel", "gifdel <media_id>", "убрать GIF из библиотеки")
def _gifdel(app, a):
    app.db.x("delete from gif_library where media_id=?", (int(a[0]),))


@cmd("calls", "calls [n]", "последние звонки (только метаданные)")
def _calls(app, a):
    table([(r["id"], r["state"], "групп." if r["grp"] else "1:1", "видео" if r["video"] else "голос", ts(r["created"]), (r["ended"] - r["started"]) if r["started"] and r["ended"] else 0)
           for r in app.db.q("select * from calls order by id desc limit ?", (int(a[0]) if a else 20,))], ["id", "состояние", "тип", "режим", "когда", "сек"])


@cmd("devices", "devices <login>", "сеансы и устройства пользователя")
def _devices(app, a):
    u = need_user(app, a[0])
    table([(r["name"] or r["device"], r["platform"], r["app"], r["ip"], ts(r["last"]), "e2e" if r["device_id"] else "") for r in app.db.q("select * from sessions where user_id=?", (u["id"],))],
          ["устройство", "платформа", "версия", "ip", "активность", "секр.чаты"])


@cmd("startx", "startx <login>", "история операций со звёздами пользователя")
def _startx(app, a):
    u = need_user(app, a[0])
    table([(ts(r["ts"]), r["amount"], r["kind"], r["descr"]) for r in app.db.q("select * from stars_tx where user_id=? order by id desc limit 30", (u["id"],))], ["когда", "звёзды", "тип", "описание"])


@cmd("webhooks", "webhooks", "вебхуки ботов")
def _webhooks(app, a):
    table([(r["bot_id"], r["url"], r["failures"], r["last_error"][:40]) for r in app.db.q("select * from webhooks")], ["bot", "url", "ошибок", "последняя ошибка"])


@cmd("limits", "limits", "лимиты Free и Premium")
def _limits(app, a):
    print(json.dumps({"free": api.DEFAULT_LIMITS["free"], "premium": api.DEFAULT_LIMITS["premium"], "override": {k: app.cfg.d.get(k) for k in ("free_limits", "premium_limits")}}, ensure_ascii=False, indent=1))


def run(app):
    print("MINI max server console. Введите help.")
    while True:
        try:
            line = input("mm> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return
        if not line:
            continue
        try:
            parts = shlex.split(line)
        except ValueError as e:
            print("ошибка разбора:", e)
            continue
        ent = CMDS.get(parts[0].lower())
        if not ent:
            print("неизвестная команда; help — список")
            continue
        try:
            ent[0](app, parts[1:])
        except SystemExit:
            return
        except PermissionError as e:
            print("ОТКАЗАНО:", e)
        except (ValueError, IndexError, KeyError) as e:
            print("ошибка:", e, "| использование:", ent[1])
        except Exception as e:
            logging.getLogger("mm").exception("console error")
            print("сбой:", e)
