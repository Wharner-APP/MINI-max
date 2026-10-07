#pragma once
#include <QString>
#include <stdexcept>

struct ConfigError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ClientConfig {
    int pingIntervalSec = 5;
    int connectTimeoutSec = 90;   // dialog "cannot connect" after this long without a server
    int requestTimeoutSec = 15;
    QString instructionsUrl;      // where connect.ip instructions live
    QString theme;                // dark | light | system
    QString dataDir;              // empty = per-user default
    QString paymentProvider;      // none | server
    static ClientConfig load(const QString &path);  // every key required; throws ConfigError
};
