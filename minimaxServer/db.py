import sqlite3, threading

SCHEMA = """
create table if not exists users(
 id integer primary key autoincrement, login text unique collate nocase not null, name text not null,
 bio text not null default '', auth text not null, salt blob not null, pub text not null default '',
 created integer not null, banned integer not null default 0, ban_reason text not null default '',
 stars integer not null default 0, premium_until integer not null default 0, business_until integer not null default 0,
 fingerprint text, device text, last_seen integer not null default 0, deleted integer not null default 0);
create index if not exists ix_users_fp on users(fingerprint);
create table if not exists sessions(token text primary key, user_id integer not null, device text, created integer, last integer);
create index if not exists ix_sessions_user on sessions(user_id);
create table if not exists chats(id integer primary key autoincrement, kind text not null, title text not null default '',
 username text unique collate nocase, about text not null default '', public integer not null default 0, owner_id integer,
 created integer, banned integer not null default 0, ban_reason text not null default '', dm_key text unique);
create table if not exists members(chat_id integer not null, user_id integer not null, role text not null default 'member',
 key_blob text not null default '', key_from text not null default '', since_msg integer not null default 0, joined integer,
 primary key(chat_id, user_id));
create index if not exists ix_members_user on members(user_id);
create table if not exists messages(id integer primary key autoincrement, chat_id integer not null, sender_id integer not null,
 ts integer not null, body text not null, enc integer not null default 0, reply_to integer not null default 0, deleted integer not null default 0);
create index if not exists ix_msgs_chat on messages(chat_id, id);
create table if not exists reports(chat_id integer not null, reporter_id integer not null, msg_id integer not null default 0,
 reason text not null default '', ts integer, primary key(chat_id, reporter_id));
create table if not exists gifts(id integer primary key autoincrement, user_id integer, from_id integer, gift_id text, stars integer, ts integer);
create table if not exists payments(id text primary key, user_id integer, kind text, item text, amount_minor integer,
 currency text, recipient text, status text, created integer);
create table if not exists admin_log(id integer primary key autoincrement, ts integer, line text);
"""


SCHEMA2 = """
create table if not exists user_settings(user_id integer not null, kind text not null, data text not null default '{}', ts integer, primary key(user_id, kind));
create table if not exists contacts(user_id integer not null, contact_id integer not null, ts integer, primary key(user_id, contact_id));
create table if not exists blocks(user_id integer not null, blocked_id integer not null, ts integer, primary key(user_id, blocked_id));
create table if not exists chat_prefs(user_id integer not null, chat_id integer not null, pinned integer not null default 0, muted_until integer not null default 0,
 archived integer not null default 0, data text not null default '{}', primary key(user_id, chat_id));
create table if not exists drafts(user_id integer not null, chat_id integer not null, text text not null default '', reply_to integer not null default 0, ts integer, primary key(user_id, chat_id));
create table if not exists read_state(chat_id integer not null, user_id integer not null, last_read integer not null default 0, primary key(chat_id, user_id));
create table if not exists message_views(msg_id integer not null, user_id integer not null, primary key(msg_id, user_id));
create table if not exists reactions(msg_id integer not null, user_id integer not null, emoji text not null, ts integer, primary key(msg_id, user_id, emoji));
create table if not exists events(id integer primary key autoincrement, chat_id integer not null, kind text not null, data text not null default '{}', ts integer);
create index if not exists ix_events_chat on events(chat_id, id);
create table if not exists hides(user_id integer not null, msg_id integer not null, primary key(user_id, msg_id));
create table if not exists scheduled(id integer primary key autoincrement, chat_id integer not null, sender_id integer not null, send_at integer not null, payload text not null);
create table if not exists media(id integer primary key autoincrement, owner_id integer not null, kind text not null, name text not null default '', mime text not null default '',
 size integer not null default 0, path text not null default '', w integer not null default 0, h integer not null default 0, dur integer not null default 0,
 enc integer not null default 0, sha256 text not null default '', meta text not null default '', created integer, ready integer not null default 0);
create table if not exists sticker_packs(id integer primary key autoincrement, short_name text unique collate nocase, title text not null, owner_id integer, premium integer not null default 0,
 official integer not null default 0, kind text not null default 'sticker', created integer);
create table if not exists stickers(id integer primary key autoincrement, pack_id integer not null, media_id integer not null, emoji text not null default '', idx integer not null default 0);
create table if not exists pack_installs(user_id integer not null, pack_id integer not null, ts integer, primary key(user_id, pack_id));
create table if not exists gif_library(media_id integer primary key, tags text not null default '', ts integer);
create table if not exists saved_gifs(user_id integer not null, media_id integer not null, ts integer, primary key(user_id, media_id));
create table if not exists polls(msg_id integer primary key, question text not null, options text not null, multiple integer not null default 0, anonymous integer not null default 1,
 quiz integer not null default 0, correct integer not null default -1, explanation text not null default '', closes_at integer not null default 0, closed integer not null default 0);
create table if not exists poll_votes(msg_id integer not null, user_id integer not null, opt integer not null, primary key(msg_id, user_id, opt));
create table if not exists calls(id integer primary key autoincrement, chat_id integer not null default 0, caller_id integer not null, callee_id integer not null default 0, video integer not null default 0,
 state text not null, grp integer not null default 0, created integer, started integer not null default 0, ended integer not null default 0, rating integer not null default 0);
create table if not exists call_members(call_id integer not null, user_id integer not null, joined integer, left integer not null default 0, primary key(call_id, user_id));
create table if not exists e2e_devices(user_id integer not null, device_id text not null, sign_pub text not null, dh_pub text not null, spk_id integer not null, spk_pub text not null, spk_sig text not null,
 updated integer, primary key(user_id, device_id));
create table if not exists e2e_otk(user_id integer not null, device_id text not null, key_id integer not null, pub text not null, primary key(user_id, device_id, key_id));
create table if not exists bot_commands(bot_id integer not null, command text not null, description text not null default '', primary key(bot_id, command));
create table if not exists bot_updates(id integer primary key autoincrement, bot_id integer not null, data text not null, ts integer);
create table if not exists webhooks(bot_id integer primary key, url text not null, secret text not null default '', failures integer not null default 0, last_error text not null default '');
create table if not exists invoices(msg_id integer primary key, payload text not null, currency text not null, total integer not null, title text not null default '', paid integer not null default 0,
 paid_by integer not null default 0, charge_id text not null default '');
create table if not exists stars_tx(id integer primary key autoincrement, user_id integer not null, amount integer not null, kind text not null, ref text not null default '', ts integer, descr text not null default '');
create table if not exists paid_unlocks(user_id integer not null, msg_id integer not null, stars integer not null, ts integer, primary key(user_id, msg_id));
create table if not exists join_requests(chat_id integer not null, user_id integer not null, ts integer, primary key(chat_id, user_id));
create table if not exists inline_results(id text primary key, bot_id integer, user_id integer, data text, ts integer);
"""

COLUMNS = {
    "users": {"username": "text", "avatar_id": "integer not null default 0", "emoji_status": "text not null default ''", "name_color": "integer not null default 0",
              "birthday": "text not null default ''", "totp_secret": "text not null default ''", "is_bot": "integer not null default 0", "bot_owner": "integer not null default 0",
              "bot_token": "text", "key_backup": "text not null default ''", "webapp_url": "text not null default ''", "bot_about": "text not null default ''",
              "bot_inline": "integer not null default 0", "bot_privacy": "integer not null default 1"},
    "sessions": {"device_id": "text not null default ''", "name": "text not null default ''", "platform": "text not null default ''", "app": "text not null default ''", "ip": "text not null default ''"},
    "chats": {"avatar_id": "integer not null default 0", "invite_hash": "text", "noforwards": "integer not null default 0", "ttl": "integer not null default 0",
              "pinned_msg": "integer not null default 0", "slowmode": "integer not null default 0", "init": "text not null default ''", "paid_stars": "integer not null default 0"},
    "members": {"device_id": "text not null default ''"},
    "messages": {"media_id": "integer not null default 0", "kind": "text not null default 'text'", "meta": "text not null default ''", "edited_at": "integer not null default 0",
                 "fwd": "text not null default ''", "markup": "text not null default ''", "paid_stars": "integer not null default 0", "expires_at": "integer not null default 0",
                 "ttl": "integer not null default 0", "target_device": "text not null default ''", "via_bot": "integer not null default 0", "views": "integer not null default 0",
                 "group_id": "integer not null default 0", "from_device": "text not null default ''"},
    "gifts": {"serial": "integer not null default 0", "note": "text not null default ''", "anonymous": "integer not null default 0", "hidden": "integer not null default 0",
              "pinned": "integer not null default 0", "converted": "integer not null default 0"},
}


class DB:
    def __init__(self, path):
        self.c = sqlite3.connect(str(path), check_same_thread=False, isolation_level=None)
        self.c.row_factory = sqlite3.Row
        self.lock = threading.RLock()
        with self.lock:
            self.c.execute("pragma journal_mode=wal")
            self.c.execute("pragma synchronous=normal")
            self.c.executescript(SCHEMA)
            self.c.executescript(SCHEMA2)
            for table, cols in COLUMNS.items():
                have = {r["name"] for r in self.c.execute("pragma table_info(%s)" % table)}
                for col, ddl in cols.items():
                    if col not in have:
                        self.c.execute("alter table %s add column %s %s" % (table, col, ddl))
            self.c.execute("update users set username=login where username is null")
            self.c.execute("create unique index if not exists ix_users_username on users(username collate nocase)")
            self.c.execute("create unique index if not exists ix_chats_invite on chats(invite_hash)")
            self.c.execute("create unique index if not exists ix_users_bot_token on users(bot_token)")
            self.c.execute("create index if not exists ix_msgs_exp on messages(expires_at)")
            self.c.execute("create index if not exists ix_msgs_target on messages(target_device, id)")

    def q(self, sql, args=()):
        with self.lock:
            return self.c.execute(sql, args).fetchall()

    def one(self, sql, args=()):
        with self.lock:
            return self.c.execute(sql, args).fetchone()

    def x(self, sql, args=()):
        with self.lock:
            return self.c.execute(sql, args).lastrowid
