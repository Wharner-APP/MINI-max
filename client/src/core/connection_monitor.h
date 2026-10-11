#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class ApiClient;

// Pings /api/ping. Emits timedOut() once when the server could not be reached for `timeoutSec`.
class ConnectionMonitor : public QObject {
    Q_OBJECT
public:
    ConnectionMonitor(ApiClient *api, int intervalSec, int timeoutSec, QObject *parent = nullptr);
    void start();
    bool online() const { return m_online; }
    QString serverName() const { return m_serverName; }
signals:
    void onlineChanged(bool online);
    void timedOut();

private:
    void tick();
    ApiClient *m_api;
    QTimer m_timer;
    QElapsedTimer m_sinceOk;
    int m_timeoutSec;
    bool m_online = false;
    bool m_warned = false;
    bool m_pending = false;
    QString m_serverName;
};
