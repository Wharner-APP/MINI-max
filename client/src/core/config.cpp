#include "core/config.h"

#define TOML_ENABLE_WINDOWS_COMPAT 0
#include <toml++/toml.hpp>

#include <QFile>

namespace {
template <typename T>
T need(const toml::table &root, const char *sec, const char *key) {
    const toml::table *t = root[sec].as_table();
    if (!t) throw ConfigError(std::string("missing section [") + sec + "]");
    auto v = (*t)[key].template value<T>();
    if (!v) throw ConfigError(std::string("missing or invalid key [") + sec + "]." + key);
    return *v;
}
}  // namespace

ClientConfig ClientConfig::load(const QString &path) {
    toml::table root;
    try {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) throw ConfigError("cannot read " + path.toStdString());
        const std::string text = f.readAll().toStdString();
        root = toml::parse(text, path.toStdString());
    } catch (const toml::parse_error &e) {
        throw ConfigError(std::string(e.description()));
    }
    ClientConfig c;
    c.pingIntervalSec = need<int>(root, "network", "ping_interval_seconds");
    c.connectTimeoutSec = need<int>(root, "network", "connect_timeout_seconds");
    c.requestTimeoutSec = need<int>(root, "network", "request_timeout_seconds");
    if (c.pingIntervalSec < 1 || c.connectTimeoutSec < 5 || c.requestTimeoutSec < 1) throw ConfigError("invalid [network] values");
    c.instructionsUrl = QString::fromStdString(need<std::string>(root, "support", "instructions_url"));
    c.theme = QString::fromStdString(need<std::string>(root, "ui", "theme"));
    if (c.theme != "dark" && c.theme != "light" && c.theme != "system") throw ConfigError("[ui].theme must be dark, light or system");
    c.dataDir = QString::fromStdString(need<std::string>(root, "storage", "data_dir"));
    c.paymentProvider = QString::fromStdString(need<std::string>(root, "payments", "provider"));
    if (c.paymentProvider != "none" && c.paymentProvider != "server") throw ConfigError("[payments].provider must be none or server");
    return c;
}
