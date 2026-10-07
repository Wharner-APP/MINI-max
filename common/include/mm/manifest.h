#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// Update manifest served by the server at /update/<platform>/manifest.json
// (detached Ed25519 signature of the exact manifest bytes at manifest.sig).
struct ManifestFile {
    QString path;    // relative, forward slashes
    QString sha256;  // lowercase hex
    qint64 size = 0;
};

struct Manifest {
    bool valid = false;
    QString error;
    QString platform;
    QString revision;  // changes on every "upload update" on the server
    QVector<ManifestFile> files;
    QStringList removed;
    static Manifest parse(const QByteArray &json);
};

// Rejects absolute paths, "..", drive letters, backslashes, empty segments.
bool isSafeRelativePath(const QString &p);
// Paths the updater never touches (user specific).
bool isProtectedPath(const QString &p);
// Files that are missing or whose SHA-256 differs from the manifest.
QVector<ManifestFile> filesToUpdate(const Manifest &m, const QString &baseDir);
QString fileSha256(const QString &path);
// Verifies a detached Ed25519 signature (libsodium). Keys/signature are base64.
bool verifyManifestSignature(const QByteArray &manifestBytes, const QByteArray &sigBase64, const QByteArray &pubKeyBase64);
