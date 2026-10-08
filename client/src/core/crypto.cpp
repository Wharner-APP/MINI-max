#include "core/crypto.h"

#include <sodium.h>

#include <algorithm>

namespace mmcrypto {

static const unsigned char *u(const QByteArray &b) { return reinterpret_cast<const unsigned char *>(b.constData()); }
static unsigned char *w(QByteArray &b) { return reinterpret_cast<unsigned char *>(b.data()); }

bool init() { return sodium_init() >= 0; }

DerivedKeys deriveFromPassword(const QString &login, const QString &password) {
    DerivedKeys k;
    if (!init()) return k;
    const QByteArray lg = login.trimmed().toLower().toUtf8();
    QByteArray salt(crypto_pwhash_SALTBYTES, 0);
    crypto_generichash(w(salt), salt.size(), u(lg), static_cast<unsigned long long>(lg.size()),
                       reinterpret_cast<const unsigned char *>("mm-salt-v1"), 10);
    const QByteArray pw = password.toUtf8();
    QByteArray master(crypto_kdf_KEYBYTES, 0);
    if (crypto_pwhash(w(master), master.size(), pw.constData(), static_cast<unsigned long long>(pw.size()), u(salt),
                      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_ARGON2ID13) != 0)
        return k;
    k.authKey.resize(32);
    k.localKey.resize(32);
    crypto_kdf_derive_from_key(w(k.authKey), 32, 1, "mmauth01", u(master));
    crypto_kdf_derive_from_key(w(k.localKey), 32, 2, "mmlocal1", u(master));
    sodium_memzero(master.data(), static_cast<size_t>(master.size()));
    return k;
}

KeyPair generateKeyPair() {
    KeyPair kp;
    if (!init()) return kp;
    kp.priv.resize(crypto_box_SECRETKEYBYTES);
    kp.pub.resize(crypto_box_PUBLICKEYBYTES);
    crypto_box_keypair(w(kp.pub), w(kp.priv));
    return kp;
}

QByteArray sharedKey(const QByteArray &myPriv, const QByteArray &myPub, const QByteArray &theirPub) {
    if (!init() || myPriv.size() != int(crypto_scalarmult_SCALARBYTES) || theirPub.size() != int(crypto_scalarmult_BYTES)) return {};
    QByteArray raw(crypto_scalarmult_BYTES, 0);
    if (crypto_scalarmult(w(raw), u(myPriv), u(theirPub)) != 0) return {};
    // order the public keys so both sides hash identical input
    QByteArray a = myPub, b = theirPub;
    if (std::lexicographical_compare(a.constBegin(), a.constEnd(), b.constBegin(), b.constEnd())) std::swap(a, b);
    QByteArray in = raw + a + b;
    QByteArray out(32, 0);
    crypto_generichash(w(out), out.size(), u(in), static_cast<unsigned long long>(in.size()), nullptr, 0);
    sodium_memzero(raw.data(), static_cast<size_t>(raw.size()));
    return out;
}

QByteArray seal(const QByteArray &key, const QByteArray &plain) {
    if (!init() || key.size() != int(crypto_aead_xchacha20poly1305_ietf_KEYBYTES)) return {};
    QByteArray out(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, 0);
    randombytes_buf(w(out), crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    unsigned long long clen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(w(out) + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, &clen, u(plain),
                                               static_cast<unsigned long long>(plain.size()), nullptr, 0, nullptr, u(out), u(key));
    out.resize(static_cast<int>(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + clen));
    return out;
}

QByteArray open(const QByteArray &key, const QByteArray &blob, bool *ok) {
    if (ok) *ok = false;
    const int np = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, ab = crypto_aead_xchacha20poly1305_ietf_ABYTES;
    if (!init() || key.size() != int(crypto_aead_xchacha20poly1305_ietf_KEYBYTES) || blob.size() < np + ab) return {};
    QByteArray out(blob.size() - np - ab, 0);
    unsigned long long mlen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(w(out), &mlen, nullptr, u(blob) + np, static_cast<unsigned long long>(blob.size() - np),
                                                   nullptr, 0, u(blob), u(key)) != 0)
        return {};
    out.resize(static_cast<int>(mlen));
    if (ok) *ok = true;
    return out;
}

QByteArray deviceKey(const QString &machineId) {
    QByteArray out(32, 0);
    if (!init()) return out;
    const QByteArray in = machineId.toUtf8();
    crypto_generichash(w(out), 32, u(in), static_cast<unsigned long long>(in.size()),
                       reinterpret_cast<const unsigned char *>("mm-device-key-v1"), 16);
    return out;
}

}  // namespace mmcrypto
