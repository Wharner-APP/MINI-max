#include "core/install_markers.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <algorithm>

namespace InstallMarkers {

QString markerFileName() {
    const QByteArray h = QCryptographicHash::hash("wharner-minimax-device-marker-v1", QCryptographicHash::Sha256).toHex();
    return QString(".mm-") + QString::fromLatin1(h.left(12)) + ".dat";
}

QStringList candidateFolders() {
    QStringList roots;
    for (auto loc : {QStandardPaths::DocumentsLocation, QStandardPaths::PicturesLocation, QStandardPaths::MusicLocation,
                     QStandardPaths::MoviesLocation, QStandardPaths::DownloadLocation, QStandardPaths::DesktopLocation,
                     QStandardPaths::AppDataLocation, QStandardPaths::GenericDataLocation, QStandardPaths::GenericConfigLocation})
        roots << QStandardPaths::writableLocation(loc);
    roots << QDir::homePath();
    roots.removeAll(QString());
    roots.removeDuplicates();
    QStringList out;
    for (const QString &r : roots) {
        if (!QFileInfo(r).isDir()) continue;
        out << r;
        const QStringList subs = QDir(r).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
        int n = 0;
        for (const QString &s : subs) {
            if (n++ >= 12) break;
            out << r + "/" + s;
        }
    }
    out.removeDuplicates();
    return out;
}

bool present() {
    const QString name = markerFileName();
    for (const QString &d : candidateFolders())
        if (QFileInfo::exists(d + "/" + name)) return true;
    return false;
}

int create(const QString &login, const QString &fingerprint) {
    QStringList cands;
    for (const QString &d : candidateFolders())
        if (QFileInfo(d).isWritable()) cands << d;
    std::shuffle(cands.begin(), cands.end(), *QRandomGenerator::global());
    QJsonObject o{{"v", 1}, {"login", login}, {"device", fingerprint}, {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    const QByteArray data = QJsonDocument(o).toJson(QJsonDocument::Compact);
    int written = 0;
    for (const QString &d : cands) {
        if (written == 3) break;
        QFile f(d + "/" + markerFileName());
        if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size()) ++written;
    }
    return written;
}

void removeAll() {
    for (const QString &d : candidateFolders()) QFile::remove(d + "/" + markerFileName());
}

}  // namespace InstallMarkers
