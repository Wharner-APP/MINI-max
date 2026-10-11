#include "mm/endpoint.h"

#include <QFile>

ServerEndpoint parseServerAddress(const QString &fileText) {
    ServerEndpoint ep;
    QString line;
    for (QString l : fileText.split('\n')) {
        l = l.trimmed();
        if (l.startsWith(QChar(0xFEFF))) l = l.mid(1).trimmed();
        if (l.isEmpty() || l.startsWith('#') || l.startsWith(';')) continue;
        line = l;
        break;
    }
    if (line.isEmpty()) { ep.error = "connect.ip is empty"; return ep; }
    if (!line.contains("://")) line = "http://" + line;
    QUrl u(line, QUrl::StrictMode);
    if (!u.isValid() || u.host().isEmpty()) { ep.error = "invalid address: " + line; return ep; }
    if (u.scheme() != "http" && u.scheme() != "https") { ep.error = "unsupported scheme: " + u.scheme(); return ep; }
    u.setUserInfo({});
    u.setQuery(QString());
    u.setFragment(QString());
    QString path = u.path();
    while (path.endsWith('/')) path.chop(1);
    u.setPath(path);
    ep.base = u;
    return ep;
}

ServerEndpoint loadServerEndpoint(const QString &configDir) {
    QFile f(configDir + "/connect.ip");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ServerEndpoint ep;
        ep.error = "cannot read " + f.fileName();
        return ep;
    }
    return parseServerAddress(QString::fromUtf8(f.readAll()));
}
