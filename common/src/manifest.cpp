#include "mm/manifest.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <sodium.h>

bool isSafeRelativePath(const QString &p) {
    if (p.isEmpty() || p.startsWith('/') || p.contains('\\') || p.contains(':') || p.contains(QChar(0))) return false;
    for (const QString &seg : p.split('/')) {
        if (seg.isEmpty() || seg == "." || seg == "..") return false;
    }
    return true;
}

bool isProtectedPath(const QString &p) { return p == "configs/connect.ip"; }

Manifest Manifest::parse(const QByteArray &json) {
    Manifest m;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) { m.error = "manifest is not valid JSON"; return m; }
    const QJsonObject o = doc.object();
    m.platform = o["platform"].toString();
    m.revision = o["revision"].toString();
    if (m.revision.isEmpty()) { m.error = "manifest has no revision"; return m; }
    for (const QJsonValue &v : o["files"].toArray()) {
        const QJsonObject f = v.toObject();
        ManifestFile mf;
        mf.path = f["path"].toString();
        mf.sha256 = f["sha256"].toString().toLower();
        mf.size = static_cast<qint64>(f["size"].toDouble());
        if (!isSafeRelativePath(mf.path) || mf.sha256.size() != 64) { m.error = "unsafe or malformed file entry: " + mf.path; return m; }
        m.files.push_back(mf);
    }
    for (const QJsonValue &v : o["removed"].toArray()) {
        const QString p = v.toString();
        if (!isSafeRelativePath(p)) { m.error = "unsafe removed entry: " + p; return m; }
        m.removed << p;
    }
    m.valid = true;
    return m;
}

QString fileSha256(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return {};
    return QString::fromLatin1(h.result().toHex());
}

QVector<ManifestFile> filesToUpdate(const Manifest &m, const QString &baseDir) {
    QVector<ManifestFile> out;
    for (const ManifestFile &f : m.files) {
        if (isProtectedPath(f.path)) continue;
        const QString local = baseDir + "/" + f.path;
        if (!QFileInfo::exists(local) || fileSha256(local) != f.sha256) out.push_back(f);
    }
    return out;
}

bool verifyManifestSignature(const QByteArray &data, const QByteArray &sigB64, const QByteArray &pubB64) {
    if (sodium_init() < 0) return false;
    const QByteArray sig = QByteArray::fromBase64(sigB64.trimmed());
    const QByteArray pub = QByteArray::fromBase64(pubB64.trimmed());
    if (sig.size() != int(crypto_sign_BYTES) || pub.size() != int(crypto_sign_PUBLICKEYBYTES)) return false;
    return crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(sig.constData()),
                                       reinterpret_cast<const unsigned char *>(data.constData()),
                                       static_cast<unsigned long long>(data.size()),
                                       reinterpret_cast<const unsigned char *>(pub.constData())) == 0;
}
