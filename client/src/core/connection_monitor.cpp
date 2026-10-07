#include "core/connection_monitor.h"

#include "core/api.h"

ConnectionMonitor::ConnectionMonitor(ApiClient *api, int intervalSec, int timeoutSec, QObject *parent)
    : QObject(parent), m_api(api), m_timeoutSec(timeoutSec) {
    m_timer.setInterval(intervalSec * 1000);
    connect(&m_timer, &QTimer::timeout, this, &ConnectionMonitor::tick);
}

void ConnectionMonitor::start() {
    m_sinceOk.start();
    m_timer.start();
    tick();
}

void ConnectionMonitor::tick() {
    if (m_pending) return;
    m_pending = true;
    m_api->get("/api/ping", this, [this](const ApiResult &r) {
        m_pending = false;
        if (r.ok) {
            m_sinceOk.restart();
            m_warned = false;
            m_serverName = r.json.value("server").toString();
            if (!m_online) { m_online = true; emit onlineChanged(true); }
            return;
        }
        if (m_online) { m_online = false; emit onlineChanged(false); }
        if (!m_warned && m_sinceOk.elapsed() >= qint64(m_timeoutSec) * 1000) {
            m_warned = true;
            emit timedOut();
        }
    });
}
