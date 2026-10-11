#include "mm/paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace MmPaths {

QString baseDir() {
    QDir d(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    if (d.absolutePath().endsWith(".app/Contents/MacOS")) {
        d.cdUp(); d.cdUp(); d.cdUp();
    }
#endif
    return d.absolutePath();
}

QString configDir() {
    const QString b = baseDir();
    const QStringList cands = {b + "/configs", b + "/../configs", b + "/../../configs", b + "/../../../configs",
                               QDir::currentPath() + "/configs"};
    for (const QString &c : cands)
        if (QFileInfo::exists(c + "/client.toml") || QFileInfo::exists(c + "/connect.ip"))
            return QDir::cleanPath(c);
    return QDir::cleanPath(b + "/configs");
}

QString dataDir(const QString &override) {
    QString p = override;
    if (p.isEmpty()) p = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (p.startsWith("~")) p = QDir::homePath() + p.mid(1);
    QDir().mkpath(p);
    return QDir::cleanPath(p);
}

}  // namespace MmPaths
