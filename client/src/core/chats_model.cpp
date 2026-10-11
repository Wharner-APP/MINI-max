#include "core/chats_model.h"

#include <QJsonArray>
#include <QSet>
#include <QHash>
#include <algorithm>
#include <QLocale>

QVariant ChatsModel::data(const QModelIndex &index, int role) const {
    const Chat *c = chat(index.row());
    if (!c) return {};
    const Message *last = c->messages.isEmpty() ? nullptr : &c->messages.last();
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return c->title;
    case LastTextRole: return last ? last->text.simplified() : QString();
    case TimeRole: {
        if (!last) return QString();
        const QDate today = QDate::currentDate(), d = last->time.date();
        if (d == today) return last->time.time().toString("HH:mm");
        if (d.daysTo(today) < 7) return QLocale(QLocale::Russian).dayName(d.dayOfWeek(), QLocale::ShortFormat);
        return d.toString("dd.MM.yy");
    }
    case UnreadRole: return c->unread;
    case PinnedRole: return c->pinned;
    case MutedRole: return c->muted;
    case IdRole: return QVariant::fromValue(c->id);
    case KindRole: return int(c->kind);
    case ArchivedRole: return c->archived;
    case VerifiedRole: return c->verified;
    case SortTimeRole: return last ? last->time : QDateTime();
    case FoldersRole: return c->folders;
    case LastOutgoingRole: return last && last->outgoing;
    case LastStatusRole: return last ? last->status : 0;
    case AvatarIdRole: return QVariant::fromValue(c->avatarId ? c->avatarId : c->peerAvatarId);
    }
    return {};
}

int ChatsModel::addChat(const Chat &src) {
    const int row = m_chats.size();
    beginInsertRows({}, row, row);
    Chat c = src;
    if (c.id <= 0) c.id = m_nextChatId++;
    else m_nextChatId = qMax(m_nextChatId, c.id + 1);
    m_chats.append(c);
    endInsertRows();
    return row;
}

int ChatsModel::addChat(const QString &title, ChatKind kind) {
    Chat c;
    c.title = title;
    c.kind = kind;
    return addChat(c);
}

void ChatsModel::addMessage(int row, const Message &src) {
    if (row < 0 || row >= m_chats.size()) return;
    Message m = src;
    if (m.id <= 0) m.id = m_nextMsgId++;
    else m_nextMsgId = qMax(m_nextMsgId, m.id + 1);
    Chat &c = m_chats[row];
    c.messages.append(m);
    if (!m.outgoing) c.unread++;
    const QModelIndex ix = index(row);
    emit dataChanged(ix, ix);
    emit messageAppended(row);
}

void ChatsModel::addServerMessage(int row, const Message &m) {
    if (row < 0 || row >= m_chats.size()) return;
    for (const Message &ex : m_chats[row].messages)
        if (ex.id == m.id && m.id > 0) return;
    Message copy = m;
    if (copy.id <= 0) copy.id = m_nextMsgId++;
    else m_nextMsgId = qMax(m_nextMsgId, copy.id + 1);
    m_chats[row].messages.append(copy);
    if (!copy.outgoing) m_chats[row].unread++;
    const QModelIndex ix = index(row);
    emit dataChanged(ix, ix);
    emit messageAppended(row);
}

Message &ChatsModel::appendMessage(int row, const QString &text, bool outgoing) {
    Message m;
    m.outgoing = outgoing;
    m.text = text;
    m.time = QDateTime::currentDateTime();
    m.status = outgoing ? 1 : 2;
    addMessage(row, m);
    return m_chats[row].messages.last();
}

int ChatsModel::rowOfId(qint64 id) const {
    for (int i = 0; i < m_chats.size(); ++i) if (m_chats[i].id == id) return i;
    return -1;
}

void ChatsModel::touch(int row) {
    if (row < 0 || row >= m_chats.size()) return;
    const QModelIndex ix = index(row);
    emit dataChanged(ix, ix);
}

void ChatsModel::markRead(int row) {
    if (row < 0 || row >= m_chats.size() || m_chats[row].unread == 0) return;
    m_chats[row].unread = 0;
    touch(row);
}

void ChatsModel::setMuted(int row, bool m) { if (auto *c = chat(row)) { c->muted = m; touch(row); } }
void ChatsModel::setArchived(int row, bool a) { if (auto *c = chat(row)) { c->archived = a; touch(row); } }

void ChatsModel::purgeOlderThan(int row, int seconds) {
    Chat *c = chat(row);
    if (!c || seconds <= 0) return;
    const QDateTime limit = QDateTime::currentDateTime().addSecs(-seconds);
    const int before = c->messages.size();
    c->messages.erase(std::remove_if(c->messages.begin(), c->messages.end(),
                                     [&](const Message &m) { return m.time < limit; }),
                      c->messages.end());
    if (c->messages.size() != before) { touch(row); emit messagesReset(row); }
}

void ChatsModel::clearAll() {
    beginResetModel();
    m_chats.clear();
    endResetModel();
}

void ChatsModel::removeChat(int row) {
    if (row < 0 || row >= m_chats.size()) return;
    beginRemoveRows({}, row, row);
    m_chats.removeAt(row);
    endRemoveRows();
}

int ChatsModel::unreadChats(bool includeMuted) const {
    int n = 0;
    for (const Chat &c : m_chats) if (c.unread > 0 && (includeMuted || !c.muted)) ++n;
    return n;
}

int ChatsModel::upsertServerChat(const Chat &c) {
    const int existing = rowOfId(c.id);
    if (existing >= 0) {
        mergeServerChat(existing, c, c.messages);
        return existing;
    }
    return addChat(c);
}

void ChatsModel::mergeServerChat(int row, const Chat &meta, const QVector<Message> &history) {
    if (row < 0 || row >= m_chats.size()) return;
    Chat &c = m_chats[row];
    c.title = meta.title.isEmpty() ? c.title : meta.title;
    c.username = meta.username;
    c.about = meta.about;
    c.kind = meta.kind;
    c.members = meta.members;
    c.avatarId = meta.avatarId;
    c.isPublic = meta.isPublic;
    c.role = meta.role;
    c.pinnedMsg = meta.pinnedMsg;
    c.autoDeleteSec = meta.autoDeleteSec;
    c.peerLogin = meta.peerLogin;
    c.peerName = meta.peerName;
    c.peerId = meta.peerId;
    c.peerAvatarId = meta.peerAvatarId;
    c.peerPub = meta.peerPub;
    if (meta.unread > 0) c.unread = meta.unread;
    c.muted = meta.muted;
    c.pinned = meta.pinned;
    c.archived = meta.archived;
    if (!history.isEmpty()) {
        // Merge by id, keep order
        QHash<qint64, int> have;
        for (int i = 0; i < c.messages.size(); ++i) have[c.messages[i].id] = i;
        for (const Message &m : history) {
            if (have.contains(m.id)) {
                c.messages[have[m.id]] = m;
            } else {
                c.messages.append(m);
                m_nextMsgId = qMax(m_nextMsgId, m.id + 1);
            }
        }
        std::sort(c.messages.begin(), c.messages.end(),
                  [](const Message &a, const Message &b) { return a.id < b.id; });
        emit messagesReset(row);
    }
    touch(row);
}

void ChatsModel::replaceMessageId(int row, qint64 localId, qint64 serverId, int status) {
    Chat *c = chat(row);
    if (!c) return;
    if (serverId != localId) {
        // The sync loop may already have delivered the server copy of this message: drop the local duplicate.
        bool serverCopyExists = false;
        for (const Message &m : c->messages)
            if (m.id == serverId) { serverCopyExists = true; break; }
        if (serverCopyExists) {
            for (int i = 0; i < c->messages.size(); ++i) {
                if (c->messages[i].id == localId) {
                    c->messages.removeAt(i);
                    break;
                }
            }
            m_nextMsgId = qMax(m_nextMsgId, serverId + 1);
            touch(row);
            emit messagesReset(row);
            return;
        }
    }
    for (Message &m : c->messages) {
        if (m.id == localId) {
            m.id = serverId;
            m.status = status;
            m_nextMsgId = qMax(m_nextMsgId, serverId + 1);
            touch(row);
            emit messagesReset(row);
            return;
        }
    }
}

void ChatsModel::editMessage(int row, qint64 msgId, const QString &text) {
    Chat *c = chat(row);
    if (!c) return;
    for (Message &m : c->messages) {
        if (m.id == msgId) {
            m.text = text;
            m.edited = true;
            touch(row);
            emit messagesReset(row);
            return;
        }
    }
}

void ChatsModel::deleteMessages(int row, const QVector<qint64> &ids) {
    Chat *c = chat(row);
    if (!c || ids.isEmpty()) return;
    QSet<qint64> set(ids.begin(), ids.end());
    const int before = c->messages.size();
    c->messages.erase(std::remove_if(c->messages.begin(), c->messages.end(),
                                     [&](const Message &m) { return set.contains(m.id); }),
                      c->messages.end());
    if (c->messages.size() != before) { touch(row); emit messagesReset(row); }
}

void ChatsModel::setPeerRead(int row, qint64 msgId) {
    Chat *c = chat(row);
    if (!c) return;
    c->peerRead = msgId;
    for (Message &m : c->messages)
        if (m.outgoing && m.id <= msgId) m.status = 2;
    touch(row);
    emit messagesReset(row);
}

void ChatsModel::setTitle(int row, const QString &t) { if (auto *c = chat(row)) { c->title = t; touch(row); } }
void ChatsModel::setAbout(int row, const QString &a) { if (auto *c = chat(row)) { c->about = a; touch(row); } }
void ChatsModel::setAvatarId(int row, qint64 id) { if (auto *c = chat(row)) { c->avatarId = id; touch(row); } }
void ChatsModel::setPinnedMsg(int row, qint64 msgId, const QString &text) {
    if (auto *c = chat(row)) { c->pinnedMsg = msgId; c->pinnedText = text; touch(row); }
}

QJsonObject ChatsModel::toJson() const {
    QJsonArray arr;
    for (const Chat &c : m_chats) {
        QJsonArray msgs;
        for (const Message &m : c.messages)
            msgs.append(QJsonObject{{"id", double(m.id)}, {"out", m.outgoing}, {"sender", m.sender}, {"text", m.text},
                                    {"reply", m.replyTo}, {"edited", m.edited}, {"status", m.status},
                                    {"time", m.time.toString(Qt::ISODate)}, {"media_id", double(m.mediaId)}});
        arr.append(QJsonObject{{"id", double(c.id)}, {"kind", int(c.kind)}, {"title", c.title}, {"username", c.username},
                               {"about", c.about}, {"unread", c.unread}, {"pinned", c.pinned}, {"muted", c.muted},
                               {"archived", c.archived}, {"auto_delete", c.autoDeleteSec},
                               {"folders", QJsonArray::fromStringList(c.folders)}, {"messages", msgs},
                               {"avatar_id", double(c.avatarId)}, {"peer_login", c.peerLogin}, {"peer_name", c.peerName},
                               {"peer_id", double(c.peerId)}, {"public", c.isPublic}, {"role", c.role}});
    }
    return QJsonObject{{"next_chat", double(m_nextChatId)}, {"next_msg", double(m_nextMsgId)}, {"chats", arr}};
}

void ChatsModel::fromJson(const QJsonObject &o) {
    beginResetModel();
    m_chats.clear();
    m_nextChatId = qint64(o["next_chat"].toDouble(1));
    m_nextMsgId = qint64(o["next_msg"].toDouble(1));
    for (const QJsonValue &v : o["chats"].toArray()) {
        const QJsonObject jo = v.toObject();
        Chat c;
        c.id = qint64(jo["id"].toDouble());
        c.kind = ChatKind(jo["kind"].toInt());
        c.title = jo["title"].toString();
        c.username = jo["username"].toString();
        c.about = jo["about"].toString();
        c.unread = jo["unread"].toInt();
        c.pinned = jo["pinned"].toBool();
        c.muted = jo["muted"].toBool();
        c.archived = jo["archived"].toBool();
        c.autoDeleteSec = jo["auto_delete"].toInt();
        c.avatarId = qint64(jo["avatar_id"].toDouble());
        c.peerLogin = jo["peer_login"].toString();
        c.peerName = jo["peer_name"].toString();
        c.peerId = qint64(jo["peer_id"].toDouble());
        c.isPublic = jo["public"].toBool();
        c.role = jo["role"].toString();
        for (const QJsonValue &fv : jo["folders"].toArray()) c.folders.append(fv.toString());
        for (const QJsonValue &mv : jo["messages"].toArray()) {
            const QJsonObject mo = mv.toObject();
            Message m;
            m.id = qint64(mo["id"].toDouble());
            m.outgoing = mo["out"].toBool();
            m.sender = mo["sender"].toString();
            m.text = mo["text"].toString();
            m.replyTo = mo["reply"].toString();
            m.edited = mo["edited"].toBool();
            m.status = mo["status"].toInt();
            m.time = QDateTime::fromString(mo["time"].toString(), Qt::ISODate);
            m.mediaId = qint64(mo["media_id"].toDouble());
            c.messages.append(m);
        }
        m_chats.append(c);
    }
    endResetModel();
}

// ---- proxy ----
ChatFilterProxy::ChatFilterProxy(QObject *parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    sort(0);
}

void ChatFilterProxy::setFolder(const QString &id) { m_folder = id; invalidateFilter(); }
void ChatFilterProxy::setArchiveView(bool a) { m_archive = a; invalidateFilter(); }
void ChatFilterProxy::setSearch(const QString &s) { m_search = s.trimmed(); invalidateFilter(); }

bool ChatFilterProxy::filterAcceptsRow(int row, const QModelIndex &parent) const {
    Q_UNUSED(parent);
    const QModelIndex ix = sourceModel()->index(row, 0);
    if (ix.data(ChatsModel::ArchivedRole).toBool() != m_archive) return false;
    if (!m_search.isEmpty() && !ix.data(ChatsModel::TitleRole).toString().contains(m_search, Qt::CaseInsensitive)
        && !ix.data(ChatsModel::LastTextRole).toString().contains(m_search, Qt::CaseInsensitive))
        return false;
    if (m_folder == "all" || m_folder.isEmpty()) return true;
    const int k = ix.data(ChatsModel::KindRole).toInt();
    if (m_folder == "groups") return k == int(ChatKind::Group);
    if (m_folder == "private") return k == int(ChatKind::Private);
    if (m_folder == "bots") return k == int(ChatKind::Bot);
    if (m_folder == "channels") return k == int(ChatKind::Channel);
    if (m_folder.startsWith("custom:")) {
        const QString fid = m_folder.mid(7);
        return ix.data(ChatsModel::FoldersRole).toStringList().contains(fid);
    }
    return true;
}

bool ChatFilterProxy::lessThan(const QModelIndex &l, const QModelIndex &r) const {
    const bool lp = l.data(ChatsModel::PinnedRole).toBool();
    const bool rp = r.data(ChatsModel::PinnedRole).toBool();
    if (lp != rp) return lp;  // pinned first (proxy sorts ascending)
    const QDateTime lt = l.data(ChatsModel::SortTimeRole).toDateTime();
    const QDateTime rt = r.data(ChatsModel::SortTimeRole).toDateTime();
    return lt > rt;  // newest first
}
