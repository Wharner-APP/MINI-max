#pragma once
#include <QByteArray>
#include <QString>

// libsodium wrappers: Argon2id password derivation, X25519 key agreement, XChaCha20-Poly1305 sealing.
namespace mmcrypto {
bool init();

struct DerivedKeys {
    QByteArray authKey;   // sent to the server instead of the password (32 bytes)
    QByteArray localKey;  // encrypts local data, never leaves the device (32 bytes)
};
DerivedKeys deriveFromPassword(const QString &login, const QString &password);

struct KeyPair { QByteArray pub, priv; };
KeyPair generateKeyPair();
// Shared secret for a conversation between two identities (symmetric for both parties).
QByteArray sharedKey(const QByteArray &myPriv, const QByteArray &myPub, const QByteArray &theirPub);

// Authenticated encryption; output = nonce(24) || ciphertext+tag.
QByteArray seal(const QByteArray &key, const QByteArray &plain);
QByteArray open(const QByteArray &key, const QByteArray &blob, bool *ok = nullptr);

// Binds a secret to this machine (used for "stay signed in").
QByteArray deviceKey(const QString &machineId);
}  // namespace mmcrypto
