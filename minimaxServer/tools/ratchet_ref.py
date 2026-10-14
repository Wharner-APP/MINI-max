"""Reference implementation of the secret-chat cryptography used with the server's /api/e2e/* and /api/secret/* endpoints.
  * X3DH key agreement (Signal specification, https://signal.org/docs/specifications/x3dh/) with X25519 identity / signed / one-time prekeys;
  * Double Ratchet (https://signal.org/docs/specifications/doubleratchet/): X25519 DH ratchet, HKDF-SHA256 root KDF, HMAC-SHA256 chain KDF,
    XChaCha20-Poly1305 message encryption, skipped-message-key store.
Port this logic to the C++ client with libsodium (crypto_scalarmult, crypto_sign_*, crypto_aead_xchacha20poly1305_ietf_*).  Requires PyNaCl."""
import base64, hashlib, hmac, os, struct

from nacl import bindings as nb
from nacl.signing import SigningKey

MAX_SKIP = 1000
b64 = lambda b: base64.b64encode(b).decode()
unb64 = lambda s: base64.b64decode(s)


def dh_keypair():
    sk = os.urandom(32)
    return sk, nb.crypto_scalarmult_base(sk)


def DH(sk, pk):
    return nb.crypto_scalarmult(sk, pk)


def hkdf(ikm, salt, info, n):
    prk = hmac.new(salt or b"\0" * 32, ikm, hashlib.sha256).digest()
    out, t = b"", b""
    for i in range(1, -(-n // 32) + 1):
        t = hmac.new(prk, t + info + bytes([i]), hashlib.sha256).digest()
        out += t
    return out[:n]


# ------------------------------------------------------------------ device keys + prekey bundle
class Device:
    def __init__(self, device_id, n_otk=10):
        self.device_id = device_id
        self.sign = SigningKey.generate()
        self.ik_sk, self.ik_pk = dh_keypair()              # identity DH key
        self.spk_sk, self.spk_pk = dh_keypair()            # signed prekey
        self.spk_id = 1
        self.otk = {i: dh_keypair() for i in range(1, n_otk + 1)}

    def upload_payload(self):
        return {"sign_pub": b64(bytes(self.sign.verify_key)), "dh_pub": b64(self.ik_pk),
                "spk": {"id": self.spk_id, "pub": b64(self.spk_pk), "sig": b64(self.sign.sign(self.spk_pk).signature)},
                "otks": [{"id": i, "pub": b64(pk)} for i, (sk, pk) in self.otk.items()]}


def x3dh_initiator(me: Device, bundle):
    """Alice: returns (shared secret, associated data, init dict to send in /api/secret/create, bob's ratchet public key)."""
    ik_b, spk_b = unb64(bundle["dh_pub"]), unb64(bundle["spk"]["pub"])
    ek_sk, ek_pk = dh_keypair()
    parts = [DH(me.ik_sk, spk_b), DH(ek_sk, ik_b), DH(ek_sk, spk_b)]
    otk = bundle.get("otk")
    if otk:
        parts.append(DH(ek_sk, unb64(otk["pub"])))
    sk = hkdf(b"\xff" * 32 + b"".join(parts), None, b"MMX3DH", 32)
    init = {"ik": b64(me.ik_pk), "ek": b64(ek_pk), "spk_id": bundle["spk"]["id"], "otk_id": otk["id"] if otk else 0}
    return sk, me.ik_pk + ik_b, init, spk_b


def x3dh_responder(me: Device, init):
    ik_a, ek_a = unb64(init["ik"]), unb64(init["ek"])
    parts = [DH(me.spk_sk, ik_a), DH(me.ik_sk, ek_a), DH(me.spk_sk, ek_a)]
    if init.get("otk_id"):
        parts.append(DH(me.otk.pop(init["otk_id"])[0], ek_a))
    sk = hkdf(b"\xff" * 32 + b"".join(parts), None, b"MMX3DH", 32)
    return sk, ik_a + me.ik_pk


# ------------------------------------------------------------------ Double Ratchet
def kdf_rk(rk, dh_out):
    o = hkdf(dh_out, rk, b"MMRatchetRK", 64)
    return o[:32], o[32:]


def kdf_ck(ck):
    return hmac.new(ck, b"\x01", hashlib.sha256).digest(), hmac.new(ck, b"\x02", hashlib.sha256).digest()   # (message key, next chain key)


def aead_keys(mk):
    o = hkdf(mk, None, b"MMMessageKey", 56)
    return o[:32], o[32:]


class Ratchet:
    def __init__(self):
        self.DHs = self.DHr = self.RK = self.CKs = self.CKr = None
        self.Ns = self.Nr = self.PN = 0
        self.skipped = {}
        self.AD = b""

    @classmethod
    def init_alice(cls, sk, bob_ratchet_pub, ad):
        r = cls()
        r.DHs, r.DHr, r.AD = dh_keypair(), bob_ratchet_pub, ad
        r.RK, r.CKs = kdf_rk(sk, DH(r.DHs[0], r.DHr))
        return r

    @classmethod
    def init_bob(cls, sk, spk_pair, ad):
        r = cls()
        r.DHs, r.RK, r.AD = spk_pair, sk, ad
        return r

    def encrypt(self, plaintext: bytes) -> bytes:
        mk, self.CKs = kdf_ck(self.CKs)
        header = self.DHs[1] + struct.pack(">II", self.PN, self.Ns)
        self.Ns += 1
        key, nonce = aead_keys(mk)
        return header + nb.crypto_aead_xchacha20poly1305_ietf_encrypt(plaintext, self.AD + header, nonce, key)

    def decrypt(self, blob: bytes) -> bytes:
        header, ct = blob[:40], blob[40:]
        dh_pub, (pn, n) = header[:32], struct.unpack(">II", header[32:])
        mk = self.skipped.pop((dh_pub, n), None)
        if mk is None:
            if dh_pub != self.DHr:
                self._skip(pn)
                self._dh_ratchet(dh_pub)
            self._skip(n)
            mk, self.CKr = kdf_ck(self.CKr)
            self.Nr += 1
        key, nonce = aead_keys(mk)
        return nb.crypto_aead_xchacha20poly1305_ietf_decrypt(ct, self.AD + header, nonce, key)

    def _skip(self, until):
        if self.CKr is None:
            return
        if until - self.Nr > MAX_SKIP:
            raise ValueError("too many skipped messages")
        while self.Nr < until:
            mk, self.CKr = kdf_ck(self.CKr)
            self.skipped[(self.DHr, self.Nr)] = mk
            self.Nr += 1

    def _dh_ratchet(self, dh_pub):
        self.PN, self.Ns, self.Nr = self.Ns, 0, 0
        self.DHr = dh_pub
        self.RK, self.CKr = kdf_rk(self.RK, DH(self.DHs[0], self.DHr))
        self.DHs = dh_keypair()
        self.RK, self.CKs = kdf_rk(self.RK, DH(self.DHs[0], self.DHr))
