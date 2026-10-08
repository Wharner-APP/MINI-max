#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <QJsonObject>
#include <QSortFilterProxyModel>
#include <QVector>

struct Reaction { QString emoji; int count = 0; };

struct Message {
    qint64 id = 0;
    bool outgoing = false;
    QString sender;
    int senderColor = 0;
    QString text;
    QString replyTo;
    QString replyAuthor;
    bool edited = false;
    int status = 0;          // 0 sending, 1 sent, 2 read
    QDateTime time;
    QVector<Reaction> reactions;
    int views = 0;
    qint64 mediaId = 0;
    QString kind = "text";
};

enum class ChatKind { Private, Group, Channel, Bot, Saved };

struct Chat {
    qint64 id = 0;
    ChatKind kind = ChatKind::Private;
    QString title, username, about, pinnedText;
    QVector<Message> messages;
    int unread = 0;
    int members = 0;
    int autoDeleteSec = 0;
    bool pinned = false, muted = false, archived = false, verified = false;
    bool isPublic = false;
    QStringList folders;
    // Server-side extras
    qint64 avatarId = 0;
    QString role;
    qint64 pinnedMsg = 0;
    QString peerLogin, peerName, peerPub;
    qint64 peerId = 0, peerAvatarId = 0;
    qint64 peerRead = 0;
};

class ChatsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TitleRole = Qt::UserRole + 1, LastTextRole, TimeRole, UnreadRole, PinnedRole, MutedRole, IdRole,
                 KindRole, ArchivedRole, VerifiedRole, SortTimeRole, FoldersRole, LastOutgoingRole, LastStatusRole,
                 AvatarIdRole };
    explicit ChatsModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_chats.size(); }
    QVariant data(const QModelIndex &index, int role) const override;

    int addChat(const Chat &c);
    int addChat(const QString &title, ChatKind kind = ChatKind::Private);
    Message &appendMessage(int row, const QString &text, bool outgoing);
    void addMessage(int row, const Message &m);
    void addServerMessage(int row, const Message &m);  // keeps server id
    Chat *chat(int row) { return row >= 0 && row < m_chats.size() ? &m_chats[row] : nullptr; }
    const Chat *chat(int row) const { return row >= 0 && row < m_chats.size() ? &m_chats[row] : nullptr; }
    int rowOfId(qint64 id) const;
    void markRead(int row);
    void touch(int row);
    void setMuted(int row, bool m);
    void setArchived(int row, bool a);
    void purgeOlderThan(int row, int seconds);
    void clearAll();
    void removeChat(int row);
    int unreadChats(bool includeMuted) const;
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);
    qint64 nextMessageId() { return m_nextMsgId++; }

    // Server sync helpers
    int upsertServerChat(const Chat &c);  // insert or replace by id
    void mergeServerChat(int row, const Chat &meta, const QVector<Message> &history);
    void replaceMessageId(int row, qint64 localId, qint64 serverId, int status);
    void editMessage(int row, qint64 msgId, const QString &text);
    void deleteMessages(int row, const QVector<qint64> &ids);
    void setPeerRead(int row, qint64 msgId);
    void setTitle(int row, const QString &t);
    void setAbout(int row, const QString &a);
    void setAvatarId(int row, qint64 id);
    void setPinnedMsg(int row, qint64 msgId, const QString &text);

signals:
    void messageAppended(int row);
    void messagesReset(int row);

private:
    QVector<Chat> m_chats;
    qint64 m_nextChatId = 1, m_nextMsgId = 1;
};

class ChatFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ChatFilterProxy(QObject *parent = nullptr);
    void setFolder(const QString &id);
    void setArchiveView(bool a);
    void setSearch(const QString &s);
    QString folder() const { return m_folder; }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &l, const QModelIndex &r) const override;
private:
    QString m_folder = "all", m_search;
    bool m_archive = false;
};
