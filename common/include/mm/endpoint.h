#pragma once
#include <QString>
#include <QUrl>

// Server address read from configs/connect.ip.
struct ServerEndpoint {
    QUrl base;
    QString error;
    bool valid() const { return error.isEmpty() && base.isValid() && !base.host().isEmpty(); }
};

// Accepts: "https://host[:port]", "http://host[:port]", "host[:port]", "1.2.3.4[:port]", "bore.pub:12345".
// Lines starting with '#' or ';' and blank lines are ignored; the first remaining line is used.
// Without a scheme plain http is assumed (raw tunnels such as bore.pub do not terminate TLS).
ServerEndpoint parseServerAddress(const QString &fileText);
ServerEndpoint loadServerEndpoint(const QString &configDir);
