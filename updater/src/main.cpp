// update(.exe): runs in the background before MINI max starts. Compares the client files with the
// signed manifest on the server and replaces changed files, then launches the messenger.
#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <mm/build_info.h>
#include <mm/endpoint.h>
#include <mm/manifest.h>
#include <mm/paths.h>

namespace {

QString g_logPath;
void logLine(const QString &s) {
    QFile f(g_logPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) f.write((QDateTime::currentDateTime().toString(Qt::ISODate) + " " + s + "\n").toUtf8());
}

QByteArray fetch(QNetworkAccessManager &nam, const QUrl &url, int timeoutMs, QString *err) {
    QNetworkRequest rq(url);
    rq.setRawHeader("User-Agent", QByteArray("MINImax-update/") + MM_VERSION_STR);
    rq.setTransferTimeout(timeoutMs);
    QNetworkReply *r = nam.get(rq);
    QEventLoop loop;
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QByteArray data;
    if (r->error() == QNetworkReply::NoError) data = r->readAll();
    else if (err) *err = r->errorString();
    r->deleteLater();
    return data;
}

QUrl filesUrl(const QUrl &base, const QString &platform, const QString &path) {
    QUrl u = base;
    QString p = base.path() + "/update/" + platform + "/files/";
    for (const QString &seg : path.split('/')) p += QUrl::toPercentEncoding(seg) + "/";
    p.chop(1);
    u.setPath(p, QUrl::DecodedMode);
    u.setUrl(base.toString(QUrl::RemovePath) + p);
    return u;
}

// Windows cannot overwrite a running exe but can rename it: move the old file aside, then put the new one in place.
bool replaceFile(const QString &dst, const QString &tmp) {
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (QFile::exists(dst) && !QFile::remove(dst)) {
        QFile::remove(dst + ".old");
        if (!QFile::rename(dst, dst + ".old")) return false;
    }
    return QFile::rename(tmp, dst);
}

void launchClient(const QStringList &args) {
    const QString base = MmPaths::baseDir();
#if defined(Q_OS_WIN)
    const QString exe = base + "/minimax.exe";
#elif defined(Q_OS_MACOS)
    const QString exe = base + "/minimax.app/Contents/MacOS/minimax";
#else
    const QString exe = base + "/minimax";
#endif
    logLine("launching " + exe);
    QProcess::startDetached(exe, args, base);
}

void cleanupOld(const QString &dir) {
    QDirIterator it(dir, {"*.old", "*.mmnew"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) QFile::remove(it.next());
}

bool runUpdate() {
    const QString base = MmPaths::baseDir(), cfg = MmPaths::configDir();
    cleanupOld(base);
    const ServerEndpoint ep = loadServerEndpoint(cfg);
    if (!ep.valid()) { logLine("connect.ip invalid: " + ep.error); return false; }
    QFile keyFile(cfg + "/update_public.key");
    const QByteArray pub = keyFile.open(QIODevice::ReadOnly) ? keyFile.readAll().trimmed() : QByteArray();
    if (pub.isEmpty() || pub.startsWith("PUT_YOUR")) { logLine("update_public.key not configured - updates disabled"); return false; }

    QNetworkAccessManager nam;
    QString err;
    const QUrl root = ep.base;
    QUrl mu = root, su = root;
    const QString pre = root.path() + "/update/" + QString(MM_PLATFORM_ID);
    mu.setPath(pre + "/manifest.json");
    su.setPath(pre + "/manifest.sig");
    const QByteArray mbytes = fetch(nam, mu, 8000, &err);
    if (mbytes.isEmpty()) { logLine("manifest not available: " + err); return false; }
    const QByteArray sig = fetch(nam, su, 8000, &err);
    if (!verifyManifestSignature(mbytes, sig, pub)) { logLine("manifest signature INVALID - update rejected"); return false; }
    const Manifest m = Manifest::parse(mbytes);
    if (!m.valid) { logLine("bad manifest: " + m.error); return false; }
    if (!m.platform.isEmpty() && m.platform != MM_PLATFORM_ID) { logLine("manifest is for another platform: " + m.platform); return false; }

    const QString statePath = MmPaths::dataDir() + "/update_state.json";
    QString lastRev;
    QFile sf(statePath);
    if (sf.open(QIODevice::ReadOnly)) lastRev = QJsonDocument::fromJson(sf.readAll()).object().value("revision").toString();
    const QVector<ManifestFile> todo = filesToUpdate(m, base);
    if (todo.isEmpty() && m.removed.isEmpty() && lastRev == m.revision) { logLine("up to date (" + m.revision + ")"); return true; }
    logLine(QString("updating to %1: %2 file(s)").arg(m.revision).arg(todo.size()));

    QVector<QPair<QString, QString>> staged;   // dst, tmp
    for (const ManifestFile &f : todo) {
        const QByteArray data = fetch(nam, filesUrl(root, m.platform.isEmpty() ? QString(MM_PLATFORM_ID) : m.platform, f.path), 60000, &err);
        if (data.isEmpty() && f.size != 0) { logLine("download failed: " + f.path + " " + err); for (auto &s : staged) QFile::remove(s.second); return false; }
        if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != f.sha256.toLatin1()) {
            logLine("checksum mismatch: " + f.path);
            for (auto &s : staged) QFile::remove(s.second);
            return false;
        }
        const QString dst = base + "/" + f.path, tmp = dst + ".mmnew";
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QSaveFile out(tmp);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) { logLine("cannot write " + tmp); for (auto &s : staged) QFile::remove(s.second); return false; }
        if (f.path == "minimax" || f.path.endsWith("/MacOS/minimax") || f.path.startsWith("update") ) QFile::setPermissions(tmp, QFile::permissions(tmp) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
        staged.push_back({dst, tmp});
    }
    for (const auto &s : staged) {
        if (QFileInfo(s.first).exists()) {
            const auto perms = QFile::permissions(s.first);
            if (!replaceFile(s.first, s.second)) { logLine("cannot replace " + s.first); return false; }
            QFile::setPermissions(s.first, perms | QFile::permissions(s.first));
        } else if (!replaceFile(s.first, s.second)) { logLine("cannot place " + s.first); return false; }
    }
    for (const QString &r : m.removed) if (!isProtectedPath(r)) QFile::remove(base + "/" + r);
    QSaveFile st(statePath);
    if (st.open(QIODevice::WriteOnly)) { st.write(QJsonDocument(QJsonObject{{"revision", m.revision}}).toJson()); st.commit(); }
    logLine("update complete");
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication::setOrganizationName("WharnerApp");
    QCoreApplication::setApplicationName("MINImax");
    QCoreApplication app(argc, argv);
    g_logPath = MmPaths::dataDir() + "/update.log";
    QStringList passthrough = app.arguments().mid(1);
    const bool noLaunch = passthrough.removeAll("--no-launch") > 0;
    try {
        runUpdate();
    } catch (...) {
        logLine("unexpected error");
    }
    if (!noLaunch) launchClient(passthrough);
    return 0;
}
