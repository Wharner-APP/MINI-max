#include "core/session_store.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QDir>
#include <QSysInfo>

#include "core/chats_model.h"
#include "core/crypto.h"

namespace SessionStore {
namespace {
QByteArray devKey() { return mmcrypto::deviceKey(QString::fromLatin1(QSysInfo::machineUniqueId())); }
bool writeFile(const QString &path, const QByteArray &data) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(data);
    return f.commit();
}
QByteArray readFile(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
}  // namespace

bool save(const QString &dir, const Session &s, const QByteArray &localKey) {
    QJsonObject o{{"id", double(s.userId)}, {"login", s.login}, {"name", s.displayName}, {"bio", s.bio}, {"token", s.token},
                  {"local_key", QString::fromLatin1(localKey.toBase64())}, {"avatar_id", double(s.avatarId)}};
    return writeFile(dir + "/session.mm", mmcrypto::seal(devKey(), QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

bool load(const QString &dir, Session *s, QByteArray *localKey) {
    bool ok = false;
    const QByteArray plain = mmcrypto::open(devKey(), readFile(dir + "/session.mm"), &ok);
    if (!ok) return false;
    const QJsonObject o = QJsonDocument::fromJson(plain).object();
    s->userId = qint64(o["id"].toDouble());
    s->login = o["login"].toString();
    s->displayName = o["name"].toString();
    s->bio = o["bio"].toString();
    s->avatarId = qint64(o["avatar_id"].toDouble());
    s->token = o["token"].toString();
    *localKey = QByteArray::fromBase64(o["local_key"].toString().toLatin1());
    return s->valid() && localKey->size() == 32;
}

void clear(const QString &dir) { QFile::remove(dir + "/session.mm"); }

bool saveChats(const QString &dir, const ChatsModel &m, const QByteArray &localKey) {
    return writeFile(dir + "/chats.mmdb", mmcrypto::seal(localKey, QJsonDocument(m.toJson()).toJson(QJsonDocument::Compact)));
}

bool loadChats(const QString &dir, ChatsModel *m, const QByteArray &localKey) {
    bool ok = false;
    const QByteArray plain = mmcrypto::open(localKey, readFile(dir + "/chats.mmdb"), &ok);
    if (!ok) return false;
    m->fromJson(QJsonDocument::fromJson(plain).object());
    return true;
}
}  // namespace SessionStore

namespace SessionStore {
namespace {
QString identityPath(const QString &dir, const QString &login) {
    const QByteArray h = QCryptographicHash::hash(login.trimmed().toLower().toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString sub = dir + "/identities";
    QDir().mkpath(sub);
    return sub + "/" + QString::fromLatin1(h) + ".mm";
}
}

bool saveIdentity(const QString &dir, const QString &login, const QByteArray &localKey, const mmcrypto::KeyPair &identity) {
    if (localKey.size() != 32 || identity.pub.size() != 32 || identity.priv.size() != 32) return false;
    const QJsonObject o{
        {"login", login.trimmed().toLower()},
        {"pub", QString::fromLatin1(identity.pub.toBase64())},
        {"priv", QString::fromLatin1(identity.priv.toBase64())}
    };
    return writeFile(identityPath(dir, login), mmcrypto::seal(localKey, QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

bool loadIdentity(const QString &dir, const QString &login, const QByteArray &localKey, mmcrypto::KeyPair *identity) {
    if (!identity || localKey.size() != 32) return false;
    bool ok = false;
    const QByteArray plain = mmcrypto::open(localKey, readFile(identityPath(dir, login)), &ok);
    if (!ok) return false;
    const QJsonObject o = QJsonDocument::fromJson(plain).object();
    if (o["login"].toString().trimmed().toLower() != login.trimmed().toLower()) return false;
    identity->pub = QByteArray::fromBase64(o["pub"].toString().toLatin1());
    identity->priv = QByteArray::fromBase64(o["priv"].toString().toLatin1());
    return identity->pub.size() == 32 && identity->priv.size() == 32;
}
} // namespace SessionStore
