#include "core/engine.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QRandomGenerator>
#include <QTemporaryFile>
#include <QDir>
#include <QRegularExpression>

#include "core/api.h"
#include "core/chats_model.h"
#include "core/device_info.h"
#include "core/prefs.h"

static QByteArray b64dec(const QString &s) { return QByteArray::fromBase64(s.toLatin1()); }
static QString b64enc(const QByteArray &b) { return QString::fromLatin1(b.toBase64()); }

Engine::Engine(ApiClient *api, ChatsModel *chats, QObject *parent)
    : QObject(parent), m_api(api), m_chats(chats) {}

QString Engine::deviceId() const {
    static QString id;
    if (id.isEmpty()) {
        const DeviceInfo d = DeviceInfo::collect();
        id = d.fingerprint.left(32);
        if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(32);
    }
    return id;
}

ChatKind Engine::kindFrom(const QString &k) const {
    if (k == "group") return ChatKind::Group;
    if (k == "channel") return ChatKind::Channel;
    if (k == "bot") return ChatKind::Bot;
    if (k == "saved" || k == "self") return ChatKind::Saved;
    return ChatKind::Private;
}

void Engine::start(const Session &s, const QByteArray &localKey, const mmcrypto::KeyPair &identity) {
    stop();
    m_session = s;
    m_localKey = localKey;
    m_identity = identity;
    m_running = true;
    m_since = 0;
    m_lastEvent = -1;
    m_knownChats.clear();
    for (int i = 0; i < m_chats->rowCount(); ++i)
        if (const Chat *c = m_chats->chat(i)) m_knownChats.insert(c->id);
    publishKeys();
    loopSync();
}

void Engine::stop() {
    m_running = false;
    m_syncInFlight = false;
}

void Engine::publishKeys() {
    if (m_identity.pub.isEmpty()) return;
    m_api->post("/api/keys/publish", {{"pub", b64enc(m_identity.pub)}}, this, [](const ApiResult &) {});
}

void Engine::loopSync() {
    if (!m_running || m_syncInFlight) return;
    m_syncInFlight = true;
    QUrlQuery q;
    q.addQueryItem("since", QString::number(m_since));
    if (m_lastEvent >= 0) q.addQueryItem("ev", QString::number(m_lastEvent));
    q.addQueryItem("wait", "15");
    QStringList known;
    for (qint64 id : m_knownChats) known << QString::number(id);
    if (!known.isEmpty()) q.addQueryItem("known", known.join(','));
    m_api->getLong("/api/sync", q, 25000, this, [this](const ApiResult &r) {
        m_syncInFlight = false;
        if (!m_running) return;
        if (r.ok) handleSync(r.json);
        // Always re-schedule; backoff a bit on errors.
        QTimer::singleShot(r.ok ? 50 : 2000, this, [this] { loopSync(); });
    });
}

void Engine::handleSync(const QJsonObject &j) {
    if (j.contains("last")) m_since = qint64(j["last"].toDouble(m_since));
    if (j.contains("last_event")) m_lastEvent = qint64(j["last_event"].toDouble(m_lastEvent < 0 ? 0 : m_lastEvent));
    if (m_lastEvent < 0) m_lastEvent = 0;

    if (j.contains("me") && j["me"].isObject()) {
        const QJsonObject me = j["me"].toObject();
        AppContext::i().stars = me["stars"].toInt(AppContext::i().stars);
        if (me.contains("avatar_id"))
            AppContext::i().session.avatarId = qint64(me["avatar_id"].toDouble());
        emit meUpdated(me);
    }

    for (const QJsonValue &v : j["chats"].toArray())
        if (v.isObject()) upsertChat(v.toObject());

    for (const QJsonValue &v : j["messages"].toArray())
        if (v.isObject()) ingestMessage(v.toObject());

    for (const QJsonValue &v : j["events"].toArray())
        if (v.isObject()) handleEvent(v.toObject());

    for (const QJsonValue &v : j["ephemeral"].toArray())
        if (v.isObject()) handleEphemeral(v.toObject());
}

void Engine::upsertChat(const QJsonObject &c) {
    const qint64 id = qint64(c["id"].toDouble());
    if (id <= 0) return;
    m_knownChats.insert(id);

    Chat chat;
    chat.id = id;
    chat.kind = kindFrom(c["kind"].toString());
    chat.title = c["title"].toString();
    chat.username = c["username"].toString();
    chat.about = c["about"].toString();
    chat.members = c["members"].toInt();
    chat.avatarId = qint64(c["avatar_id"].toDouble());
    chat.isPublic = c["public"].toBool();
    chat.role = c["role"].toString();
    chat.pinnedMsg = qint64(c["pinned_msg"].toDouble());
    chat.autoDeleteSec = c["ttl"].toInt();
    chat.peerLogin = c["peer"].toObject()["login"].toString();
    chat.peerName = c["peer"].toObject()["name"].toString();
    chat.peerId = qint64(c["peer"].toObject()["id"].toDouble());
    chat.peerAvatarId = qint64(c["peer"].toObject()["avatar_id"].toDouble());
    chat.peerPub = c["peer"].toObject()["pub"].toString();

    if (chat.kind == ChatKind::Private && !chat.peerName.isEmpty())
        chat.title = chat.peerName;
    else if (chat.kind == ChatKind::Private && !chat.peerLogin.isEmpty())
        chat.title = chat.peerLogin;

    if (!chat.peerPub.isEmpty()) {
        const QByteArray raw = b64dec(chat.peerPub);
        if (!raw.isEmpty()) {
            m_peerPubs[chat.peerLogin] = raw;
            if (!m_identity.priv.isEmpty()) {
                const QByteArray sk = mmcrypto::sharedKey(m_identity.priv, m_identity.pub, raw);
                if (!sk.isEmpty()) m_chatKeys[id] = sk;
            }
        }
    }

    // History from sync
    QVector<Message> hist;
    for (const QJsonValue &v : c["history"].toArray()) {
        if (!v.isObject()) continue;
        const QJsonObject mo = v.toObject();
        Message m;
        m.id = qint64(mo["id"].toDouble());
        m.outgoing = mo["sender_id"].toDouble() == m_session.userId
                     || mo["sender"].toString() == m_session.login
                     || mo["sender"].toString() == AppContext::i().session.login;
        m.sender = mo["sender_name"].toString().isEmpty() ? mo["sender"].toString() : mo["sender_name"].toString();
        m.senderColor = int(qHash(m.sender) % 7);
        m.text = decryptBody(id, mo["body"].toString(), mo["enc"].toInt() != 0);
        m.time = QDateTime::fromSecsSinceEpoch(qint64(mo["ts"].toDouble()));
        m.status = m.outgoing ? 1 : 2;
        m.edited = mo["edited"].toBool();
        m.views = mo["views"].toInt();
        m.kind = mo["kind"].toString("text");
        m.mediaId = qint64(mo["media"].toObject()["id"].toDouble(mo["media_id"].toDouble()));
        if (mo.contains("reactions") && mo["reactions"].isArray()) {
            for (const QJsonValue &rv : mo["reactions"].toArray()) {
                const QJsonObject ro = rv.toObject();
                Reaction r; r.emoji = ro["emoji"].toString(); r.count = ro["count"].toInt();
                m.reactions.append(r);
            }
        }
        hist.append(m);
    }

    chat.unread = c["unread"].toInt();
    if (c.contains("prefs") && c["prefs"].isObject()) {
        const QJsonObject p = c["prefs"].toObject();
        chat.muted = p["muted"].toBool();
        chat.pinned = p["pinned"].toBool();
        chat.archived = p["archived"].toBool();
    }

    const int row = m_chats->rowOfId(id);
    if (row < 0) {
        if (!hist.isEmpty()) chat.messages = hist;
        m_chats->upsertServerChat(chat);
    } else {
        m_chats->mergeServerChat(row, chat, hist);
    }
}

void Engine::ingestMessage(const QJsonObject &mo) {
    const qint64 chatId = qint64(mo["chat_id"].toDouble());
    int row = m_chats->rowOfId(chatId);
    if (row < 0) {
        // Unknown chat — force known list refresh next sync by not adding to known
        m_knownChats.remove(chatId);
        return;
    }
    Message m;
    m.id = qint64(mo["id"].toDouble());
    // Dedup
    if (const Chat *c = m_chats->chat(row))
        for (const Message &ex : c->messages)
            if (ex.id == m.id) return;

    m.outgoing = mo["sender_id"].toDouble() == m_session.userId
                 || mo["sender"].toString() == m_session.login;
    m.sender = mo["sender_name"].toString().isEmpty() ? mo["sender"].toString() : mo["sender_name"].toString();
    m.senderColor = int(qHash(m.sender) % 7);
    m.text = decryptBody(chatId, mo["body"].toString(), mo["enc"].toInt() != 0);
    m.time = QDateTime::fromSecsSinceEpoch(qint64(mo["ts"].toDouble()));
    m.status = m.outgoing ? 1 : 2;
    m.edited = mo["edited"].toBool();
    m.views = mo["views"].toInt();
    m.kind = mo["kind"].toString("text");
    m.mediaId = qint64(mo["media"].toObject()["id"].toDouble(mo["media_id"].toDouble()));
    if (mo.contains("reply_to") && mo["reply_to"].toDouble() > 0) {
        m.replyTo = QString::number(qint64(mo["reply_to"].toDouble()));
    }
    m_chats->addServerMessage(row, m);
    if (m.outgoing) m_since = qMax(m_since, m.id);
}

void Engine::handleEvent(const QJsonObject &e) {
    const QString kind = e["kind"].toString();
    const qint64 chatId = qint64(e["chat_id"].toDouble());
    const QJsonObject data = e["data"].toObject();
    const int row = m_chats->rowOfId(chatId);
    if (row < 0) return;

    if (kind == "edit") {
        m_chats->editMessage(row, qint64(data["id"].toDouble()),
                             decryptBody(chatId, data["body"].toString(), data["enc"].toInt() != 0));
    } else if (kind == "delete") {
        QVector<qint64> ids;
        if (data["ids"].isArray())
            for (const QJsonValue &v : data["ids"].toArray()) ids.append(qint64(v.toDouble()));
        else if (data.contains("id")) ids.append(qint64(data["id"].toDouble()));
        m_chats->deleteMessages(row, ids);
    } else if (kind == "read") {
        m_chats->setPeerRead(row, qint64(data["msg_id"].toDouble()));
    } else if (kind == "reaction") {
        // full refresh of reactions not critical; touch
        m_chats->touch(row);
    } else if (kind == "chat_update") {
        if (data.contains("title")) m_chats->setTitle(row, data["title"].toString());
        if (data.contains("about")) m_chats->setAbout(row, data["about"].toString());
        if (data.contains("avatar_id")) m_chats->setAvatarId(row, qint64(data["avatar_id"].toDouble()));
        m_chats->touch(row);
    } else if (kind == "pin") {
        m_chats->setPinnedMsg(row, qint64(data["msg_id"].toDouble()), data["text"].toString());
    }
}

void Engine::handleEphemeral(const QJsonObject &e) {
    const QString type = e["type"].toString();
    if (type == "typing") {
        emit typingEvent(qint64(e["chat_id"].toDouble()), e["user"].toString().isEmpty() ? e["username"].toString() : e["user"].toString(), true);
    }
}

QString Engine::encryptBody(qint64 chatId, const QString &plain) {
    const QByteArray key = m_chatKeys.value(chatId);
    if (key.isEmpty()) return plain;  // public / no key yet
    const QByteArray sealed = mmcrypto::seal(key, plain.toUtf8());
    return sealed.isEmpty() ? plain : b64enc(sealed);
}

QString Engine::decryptBody(qint64 chatId, const QString &encBody, bool enc) {
    if (!enc || encBody.isEmpty()) return encBody;
    const QByteArray key = m_chatKeys.value(chatId);
    if (!key.isEmpty()) {
        bool ok = false;
        const QByteArray plain = mmcrypto::open(key, b64dec(encBody), &ok);
        if (ok) return QString::fromUtf8(plain);
    }
    // Fallback: body may already be plaintext (legacy / no peer key).
    const QByteArray raw = QByteArray::fromBase64(encBody.toLatin1());
    if (!raw.isEmpty() && raw != encBody.toLatin1()) {
        const QString s = QString::fromUtf8(raw);
        if (!s.isEmpty() && s != encBody) return s;
    }
    // If it looks like normal text, show it.
    if (!encBody.contains(QRegularExpression(QStringLiteral("^[A-Za-z0-9+/=]{20,}$"))))
        return encBody;
    return QStringLiteral("[зашифровано]");
}

void Engine::ensurePeerKey(const QString &login, std::function<void(const QByteArray &pub)> cb) {
    if (m_peerPubs.contains(login)) { cb(m_peerPubs[login]); return; }
    QUrlQuery q; q.addQueryItem("login", login);
    m_api->get("/api/keys/get", q, this, [this, login, cb](const ApiResult &r) {
        QByteArray pub;
        if (r.ok) {
            pub = b64dec(r.json["pub"].toString());
            if (!pub.isEmpty()) m_peerPubs[login] = pub;
        }
        cb(pub);
    });
}

void Engine::sendText(qint64 chatId, const QString &text, qint64 replyTo) {
    const int row = m_chats->rowOfId(chatId);
    if (row < 0 || text.trimmed().isEmpty()) return;
    Chat *c = m_chats->chat(row);
    if (!c) return;

    const bool mustEnc = (c->kind == ChatKind::Private || c->kind == ChatKind::Saved)
                         || (!c->isPublic && (c->kind == ChatKind::Group || c->kind == ChatKind::Channel));

    auto doSend = [this, chatId, row, text, replyTo, mustEnc](bool encOk) {
        QJsonObject body;
        body["chat_id"] = double(chatId);
        body["kind"] = "text";
        if (mustEnc && encOk && m_chatKeys.contains(chatId)) {
            body["enc"] = 1;
            body["body"] = encryptBody(chatId, text);
        } else {
            body["enc"] = 0;
            body["body"] = text;
        }
        if (replyTo > 0) body["reply_to"] = double(replyTo);

        // Optimistic local message
        Message local;
        local.id = m_chats->nextMessageId();
        local.outgoing = true;
        local.text = text;
        local.time = QDateTime::currentDateTime();
        local.status = 0;
        local.kind = "text";
        m_chats->addServerMessage(row, local);
        const qint64 localId = local.id;

        m_api->post("/api/messages/send", body, this, [this, row, localId](const ApiResult &r) {
            if (!r.ok) {
                emit notice("Не отправлено", r.error);
                return;
            }
            const qint64 serverId = qint64(r.json["id"].toDouble());
            m_chats->replaceMessageId(row, localId, serverId, 1);
            m_since = qMax(m_since, serverId);
        });
    };

    if (mustEnc && !m_chatKeys.contains(chatId) && !c->peerLogin.isEmpty()) {
        ensurePeerKey(c->peerLogin, [this, chatId, doSend](const QByteArray &pub) {
            if (!pub.isEmpty() && !m_identity.priv.isEmpty()) {
                const QByteArray sk = mmcrypto::sharedKey(m_identity.priv, m_identity.pub, pub);
                if (!sk.isEmpty()) m_chatKeys[chatId] = sk;
            }
            // If peer key missing, still send plaintext (server allows).
            doSend(true);
        });
    } else {
        doSend(true);
    }
}

void Engine::markRead(qint64 chatId, qint64 lastMsgId) {
    const int row = m_chats->rowOfId(chatId);
    if (row >= 0) m_chats->markRead(row);
    if (lastMsgId <= 0) return;
    m_api->post("/api/messages/read", {{"chat_id", double(chatId)}, {"msg_id", double(lastMsgId)}}, this, [](const ApiResult &) {});
}

void Engine::typing(qint64 chatId) {
    m_api->post("/api/typing", {{"chat_id", double(chatId)}, {"action", "typing"}}, this, [](const ApiResult &) {});
}

void Engine::editMessage(qint64 chatId, qint64 msgId, const QString &text) {
    m_api->post("/api/messages/edit", {{"chat_id", double(chatId)}, {"msg_id", double(msgId)}, {"body", text}}, this,
                [this, chatId, msgId, text](const ApiResult &r) {
                    if (!r.ok) { emit notice("Правка", r.error); return; }
                    const int row = m_chats->rowOfId(chatId);
                    if (row >= 0) m_chats->editMessage(row, msgId, text);
                });
}

void Engine::deleteMessages(qint64 chatId, const QVector<qint64> &ids, bool forEveryone) {
    QJsonArray arr;
    for (qint64 id : ids) arr.append(double(id));
    m_api->post("/api/messages/delete", {{"chat_id", double(chatId)}, {"ids", arr}, {"everyone", forEveryone}}, this,
                [this, chatId, ids](const ApiResult &r) {
                    if (!r.ok) { emit notice("Удаление", r.error); return; }
                    const int row = m_chats->rowOfId(chatId);
                    if (row >= 0) m_chats->deleteMessages(row, ids);
                });
}

void Engine::react(qint64 chatId, qint64 msgId, const QString &emoji) {
    m_api->post("/api/messages/react", {{"chat_id", double(chatId)}, {"msg_id", double(msgId)}, {"emoji", emoji}}, this,
                [this](const ApiResult &r) { if (!r.ok) emit notice("Реакция", r.error); });
}

void Engine::openDm(const QString &login) {
    m_api->post("/api/dm/open", {{"login", login}}, this, [this](const ApiResult &r) {
        if (!r.ok) { emit notice("Личные сообщения", r.error); return; }
        if (r.json.contains("chat") && r.json["chat"].isObject())
            upsertChat(r.json["chat"].toObject());
        else if (r.json.contains("id")) {
            // minimal: drop from known so next sync pulls history
            m_knownChats.remove(qint64(r.json["id"].toDouble()));
        }
        emit notice("Чат", "Диалог открыт");
    });
}

void Engine::createChat(const QString &kind, const QString &title, bool isPublic) {
    QJsonObject body{{"kind", kind}, {"title", title}, {"public", isPublic}};
    if (!isPublic) {
        // Server expects a member key_blob for private groups/channels.
        QByteArray kb(32, 0);
        for (int i = 0; i < 32; ++i) kb[i] = char(QRandomGenerator::global()->generate() & 0xFF);
        body["key_blob"] = QString::fromLatin1(kb.toBase64());
    }
    m_api->post("/api/chats/create", body, this, [this](const ApiResult &r) {
        if (!r.ok) { emit notice("Создание", r.error); return; }
        if (r.json.contains("chat") && r.json["chat"].isObject())
            upsertChat(r.json["chat"].toObject());
        else if (r.json.contains("id"))
            m_knownChats.remove(qint64(r.json["id"].toDouble()));
        emit notice("Готово", "Чат создан");
    });
}

void Engine::leaveChat(qint64 chatId) {
    m_api->post("/api/chats/leave", {{"chat_id", double(chatId)}}, this, [this, chatId](const ApiResult &r) {
        if (!r.ok) { emit notice("Выход", r.error); return; }
        const int row = m_chats->rowOfId(chatId);
        if (row >= 0) m_chats->removeChat(row);
        m_knownChats.remove(chatId);
    });
}

void Engine::updateChat(qint64 chatId, const QJsonObject &fields) {
    QJsonObject body = fields;
    body["chat_id"] = double(chatId);
    m_api->post("/api/chats/update", body, this, [this, chatId](const ApiResult &r) {
        if (!r.ok) { emit notice("Обновление чата", r.error); return; }
        if (r.json.contains("chat") && r.json["chat"].isObject())
            upsertChat(r.json["chat"].toObject());
    });
}

void Engine::joinByUsername(const QString &username) {
    m_api->post("/api/chats/join", {{"username", username}}, this, [this](const ApiResult &r) {
        if (!r.ok) { emit notice("Вступление", r.error); return; }
        if (r.json.contains("chat") && r.json["chat"].isObject())
            upsertChat(r.json["chat"].toObject());
    });
}

void Engine::updateProfile(const QJsonObject &fields) {
    m_api->post("/api/profile/update", fields, this, [this](const ApiResult &r) {
        if (!r.ok) { emit notice("Профиль", r.error); return; }
        if (r.json.contains("user") && r.json["user"].isObject()) {
            const QJsonObject u = r.json["user"].toObject();
            if (u.contains("display_name")) AppContext::i().session.displayName = u["display_name"].toString();
            if (u.contains("bio")) AppContext::i().session.bio = u["bio"].toString();
            if (u.contains("username")) AppContext::i().session.login = u["username"].toString();
            if (u.contains("avatar_id")) AppContext::i().session.avatarId = qint64(u["avatar_id"].toDouble());
            if (fields.contains("name_color")) Prefs::instance().set("profile/name_color_index", fields["name_color"].toInt());
        }
        emit notice("Профиль", "Сохранено");
    });
}

void Engine::uploadFile(const QString &path, const QString &kind, bool enc,
                        std::function<void(qint64 mediaId, const QString &err)> cb) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { cb(0, "Не удалось открыть файл"); return; }
    const QByteArray data = f.readAll();
    f.close();
    if (data.isEmpty()) { cb(0, "Пустой файл"); return; }

    QMimeDatabase db;
    const QString mime = db.mimeTypeForFile(path).name();
    const QString name = QFileInfo(path).fileName();
    QString q = QString("/media/upload?kind=%1&name=%2&mime=%3")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(kind)),
                         QString::fromUtf8(QUrl::toPercentEncoding(name)),
                         QString::fromUtf8(QUrl::toPercentEncoding(mime)));
    if (enc) q += "&enc=1";

    m_api->upload(q, data, mime, this, [cb](const ApiResult &r) {
        if (!r.ok) { cb(0, r.error); return; }
        cb(qint64(r.json["id"].toDouble()), {});
    });
}

void Engine::setMyAvatar(const QString &localImagePath) {
    uploadFile(localImagePath, "avatar", false, [this](qint64 mid, const QString &err) {
        if (mid <= 0) { emit notice("Аватар", err.isEmpty() ? "Ошибка загрузки" : err); return; }
        m_api->post("/api/profile/avatar", {{"media_id", double(mid)}}, this, [this, mid](const ApiResult &r) {
            if (!r.ok) { emit notice("Аватар", r.error); return; }
            emit notice("Аватар", "Фото профиля обновлено");
            downloadMedia(mid, [this, mid](const QByteArray &data, const QString &) {
                if (!data.isEmpty()) emit avatarReady(mid, data);
            });
        });
    });
}

void Engine::clearMyAvatar() {
    m_api->post("/api/profile/avatar", {{"media_id", 0}}, this, [this](const ApiResult &r) {
        emit notice("Аватар", r.ok ? "Фото удалено" : r.error);
    });
}

void Engine::setChatAvatar(qint64 chatId, const QString &localImagePath) {
    uploadFile(localImagePath, "avatar", false, [this, chatId](qint64 mid, const QString &err) {
        if (mid <= 0) { emit notice("Аватар чата", err.isEmpty() ? "Ошибка загрузки" : err); return; }
        updateChat(chatId, {{"avatar_id", double(mid)}});
        emit notice("Аватар чата", "Обновлено");
    });
}

void Engine::downloadMedia(qint64 mediaId, std::function<void(const QByteArray &, const QString &)> cb) {
    if (mediaId <= 0) { cb({}, "bad id"); return; }
    m_api->download(QString("/media/%1").arg(mediaId), this, cb);
}


void Engine::sendMediaFile(qint64 chatId, const QString &path, const QString &kind, const QString &caption,
                              std::function<void(bool, const QString &)> done) {
    const int row = m_chats->rowOfId(chatId);
    if (row < 0) { if (done) done(false, "Чат не найден"); return; }
    uploadFile(path, kind, false, [this, chatId, kind, caption, done](qint64 mid, const QString &err) {
        if (mid <= 0) {
            emit notice("Медиа", err.isEmpty() ? "Не удалось загрузить файл" : err);
            if (done) done(false, err);
            return;
        }
        QJsonObject body{{"chat_id", double(chatId)}, {"kind", kind}, {"media_id", double(mid)}, {"body", caption}, {"enc", 0}};
        m_api->post("/api/messages/send", body, this, [this, done](const ApiResult &r) {
            if (!r.ok) {
                emit notice("Медиа", r.error);
                if (done) done(false, r.error);
            } else if (done) {
                done(true, {});
            }
        });
    });
}

void Engine::sendRemoteGif(qint64 chatId, const QUrl &url, const QString &title) {
    if (!url.isValid() || url.scheme().isEmpty()) {
        emit notice("GIF", "Некорректный адрес GIF");
        return;
    }
    m_api->download(url.toString(), this, [this, chatId, title](const QByteArray &data, const QString &err) {
        if (data.isEmpty()) {
            emit notice("GIF", err.isEmpty() ? "Не удалось загрузить GIF" : err);
            return;
        }
        auto *tmp = new QTemporaryFile(QDir::tempPath() + "/minimax-gif-XXXXXX.gif");
        tmp->setAutoRemove(false);
        if (!tmp->open()) {
            delete tmp;
            emit notice("GIF", "Не удалось создать временный файл");
            return;
        }
        const QString path = tmp->fileName();
        tmp->write(data);
        tmp->close();
        delete tmp;
        uploadFile(path, "gif", false, [this, chatId, path, title](qint64 mid, const QString &uerr) {
            QFile::remove(path);
            if (mid <= 0) {
                emit notice("GIF", uerr.isEmpty() ? "Не удалось загрузить GIF на сервер" : uerr);
                return;
            }
            m_api->post("/api/messages/send",
                        {{"chat_id", double(chatId)}, {"kind", "gif"}, {"media_id", double(mid)}, {"body", title}, {"enc", 0}},
                        this, [this](const ApiResult &r) {
                            if (!r.ok) emit notice("GIF", r.error);
                        });
        });
    });
}

void Engine::sendSticker(qint64 chatId, const QString &emoji) {
    if (emoji.trimmed().isEmpty()) return;
    m_api->post("/api/messages/send",
                {{"chat_id", double(chatId)}, {"kind", "sticker"}, {"body", emoji}, {"enc", 0}},
                this, [this](const ApiResult &r) {
                    if (!r.ok) emit notice("Стикер", r.error);
                });
}

void Engine::search(const QString &query, std::function<void(const QVector<SearchHit> &)> cb) {
    const QString q = query.trimmed();
    if (q.size() < 2) { cb({}); return; }
    QUrlQuery uq; uq.addQueryItem("q", q);
    m_api->get("/api/search", uq, this, [cb](const ApiResult &r) {
        QVector<SearchHit> hits;
        if (!r.ok) { cb(hits); return; }
        for (const QJsonValue &v : r.json["users"].toArray()) {
            const QJsonObject o = v.toObject();
            SearchHit h;
            h.type = SearchHit::User;
            h.login = o["login"].toString();
            h.username = o["username"].toString();
            h.name = o["name"].toString();
            h.avatarId = qint64(o["avatar_id"].toDouble());
            h.isBot = o["is_bot"].toBool();
            hits.append(h);
        }
        for (const QJsonValue &v : r.json["chats"].toArray()) {
            const QJsonObject o = v.toObject();
            SearchHit h;
            h.type = SearchHit::Chat;
            h.id = qint64(o["id"].toDouble());
            h.kind = o["kind"].toString();
            h.title = o["title"].toString();
            h.username = o["username"].toString();
            h.members = o["members"].toInt();
            hits.append(h);
        }
        cb(hits);
    });
}

void Engine::addContact(const QString &login) {
    m_api->post("/api/contacts/add", {{"login", login}}, this, [this](const ApiResult &r) {
        emit notice("Контакты", r.ok ? "Добавлен" : r.error);
    });
}

void Engine::removeContact(qint64 userId) {
    m_api->post("/api/contacts/remove", {{"user_id", double(userId)}}, this, [this](const ApiResult &r) {
        emit notice("Контакты", r.ok ? "Удалён" : r.error);
    });
}

void Engine::blockUser(qint64 userId) {
    m_api->post("/api/blocks/add", {{"user_id", double(userId)}}, this, [this](const ApiResult &r) {
        emit notice("Блокировка", r.ok ? "Пользователь заблокирован" : r.error);
    });
}

void Engine::unblockUser(qint64 userId) {
    m_api->post("/api/blocks/remove", {{"user_id", double(userId)}}, this, [this](const ApiResult &r) {
        emit notice("Блокировка", r.ok ? "Разблокирован" : r.error);
    });
}

void Engine::loadSettings(const QString &kind, std::function<void(const QJsonObject &)> cb) {
    QUrlQuery q; q.addQueryItem("kind", kind);
    m_api->get("/api/settings", q, this, [cb](const ApiResult &r) {
        cb(r.ok ? r.json["data"].toObject() : QJsonObject());
    });
}

void Engine::saveSettings(const QString &kind, const QJsonObject &data) {
    m_api->post("/api/settings", {{"kind", kind}, {"data", data}}, this, [](const ApiResult &) {});
}
