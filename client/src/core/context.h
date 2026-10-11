#pragma once
#include <QString>
#include <functional>

class ApiClient;
class ChatsModel;
class ConnectionMonitor;
class Engine;

struct ClientConfig;

struct Session {
    qint64 userId = 0;
    QString login, displayName, bio, token;
    qint64 avatarId = 0;
    bool valid() const { return !token.isEmpty() && !login.isEmpty(); }
};

struct AppContext {
    static AppContext &i() { static AppContext c; return c; }
    ApiClient *api = nullptr;
    ChatsModel *chats = nullptr;
    ConnectionMonitor *monitor = nullptr;
    Engine *engine = nullptr;
    Session session;
    QString dataDir, instructionsUrl;
    int stars = 0;
    std::function<void()> logout;
    std::function<void(int)> openChatRow;
};
