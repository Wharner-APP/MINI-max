#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>

#include "core/chats_model.h"
#include "core/context.h"
#include "core/crypto.h"

class ApiClient;
class ChatsModel;

struct SearchHit {
    enum Type { User, Chat } type = User;
    qint64 id = 0;
    QString login, username, name, title, kind;
    qint64 avatarId = 0;
    int members = 0;
    bool isBot = false;
};

// Live session: sync long-poll, messaging, search, media, profile, groups.
class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(ApiClient *api, ChatsModel *chats, QObject *parent = nullptr);

    void start(const Session &s, const QByteArray &localKey, const mmcrypto::KeyPair &identity);
    void stop();
    bool running() const { return m_running; }

    // Messaging
    void sendText(qint64 chatId, const QString &text, qint64 replyTo = 0);
    void markRead(qint64 chatId, qint64 lastMsgId);
    void typing(qint64 chatId);
    void editMessage(qint64 chatId, qint64 msgId, const QString &text);
    void deleteMessages(qint64 chatId, const QVector<qint64> &ids, bool forEveryone = true);
    void react(qint64 chatId, qint64 msgId, const QString &emoji);

    // Chats
    void openDm(const QString &login);
    void createChat(const QString &kind, const QString &title, bool isPublic = false);
    void leaveChat(qint64 chatId);
    void updateChat(qint64 chatId, const QJsonObject &fields);
    void joinByUsername(const QString &username);

    // Profile / avatars
    void updateProfile(const QJsonObject &fields);
    void setMyAvatar(const QString &localImagePath);
    void setChatAvatar(qint64 chatId, const QString &localImagePath);
    void clearMyAvatar();

    // Search
    void search(const QString &query, std::function<void(const QVector<SearchHit> &)> cb);

    // Media
    void uploadFile(const QString &path, const QString &kind, bool enc,
                    std::function<void(qint64 mediaId, const QString &err)> cb);
    void downloadMedia(qint64 mediaId, std::function<void(const QByteArray &, const QString &)> cb);
    void sendMediaFile(qint64 chatId, const QString &path, const QString &kind, const QString &caption = {}, std::function<void(bool, const QString &)> done = {});
    void sendRemoteGif(qint64 chatId, const QUrl &url, const QString &title = {});
    void sendSticker(qint64 chatId, const QString &emoji);

    // Contacts / blocks
    void addContact(const QString &login);
    void removeContact(qint64 userId);
    void blockUser(qint64 userId);
    void unblockUser(qint64 userId);

    // Settings
    void loadSettings(const QString &kind, std::function<void(const QJsonObject &)> cb);
    void saveSettings(const QString &kind, const QJsonObject &data);

    QByteArray identityPub() const { return m_identity.pub; }

signals:
    void notice(const QString &title, const QString &text);
    void searchResults(const QVector<SearchHit> &hits);
    void meUpdated(const QJsonObject &me);
    void avatarReady(qint64 mediaId, const QByteArray &png);
    void typingEvent(qint64 chatId, const QString &user, bool on);

private:
    void loopSync();
    void handleSync(const QJsonObject &j);
    void upsertChat(const QJsonObject &c);
    void ingestMessage(const QJsonObject &m);
    void handleEvent(const QJsonObject &e);
    void handleEphemeral(const QJsonObject &e);
    void publishKeys();
    void ensurePeerKey(const QString &login, std::function<void(const QByteArray &pub)> cb, int attempt = 0);
    QString encryptBody(qint64 chatId, const QString &plain);
    QString decryptBody(qint64 chatId, const QString &encBody, bool enc);
    ChatKind kindFrom(const QString &k) const;
    QString deviceId() const;

    ApiClient *m_api;
    ChatsModel *m_chats;
    Session m_session;
    QByteArray m_localKey;
    mmcrypto::KeyPair m_identity;
    bool m_running = false;
    bool m_syncInFlight = false;
    qint64 m_since = 0;
    qint64 m_lastEvent = -1;  // -1 = first call (server returns only cursor)
    QHash<QString, QByteArray> m_peerPubs;  // login -> pub raw
    QHash<qint64, QByteArray> m_chatKeys;   // chat_id -> shared key for private
    QSet<qint64> m_knownChats;
};
