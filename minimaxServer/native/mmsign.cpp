// mmsign: Ed25519 signer for client update manifests (libsodium).
//   mmsign genkeys <dir>                    -> <dir>/update_private.key, update_public.key (base64, 32-byte seed / public key)
//   mmsign sign <private.key> <file>        -> prints base64 detached signature of <file>
//   mmsign verify <public.key> <file> <sig_base64>
#include <sodium.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static std::string b64(const unsigned char *d, size_t n) {
    std::string out(sodium_base64_ENCODED_LEN(n, sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(out.data(), out.size(), d, n, sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}
static bool unb64(const std::string &s, std::vector<unsigned char> &out, size_t max) {
    out.assign(max, 0);
    size_t len = 0;
    if (sodium_base642bin(out.data(), out.size(), s.c_str(), s.size(), "\r\n \t", &len, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) return false;
    out.resize(len);
    return true;
}
static std::string readText(const std::string &p) { std::ifstream f(p); return std::string((std::istreambuf_iterator<char>(f)), {}); }
static std::vector<unsigned char> readBin(const std::string &p) { std::ifstream f(p, std::ios::binary); return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), {}); }

int main(int argc, char **argv) {
    if (sodium_init() < 0) return 2;
    if (argc >= 3 && !std::strcmp(argv[1], "genkeys")) {
        unsigned char pk[crypto_sign_PUBLICKEYBYTES], sk[crypto_sign_SECRETKEYBYTES], seed[crypto_sign_SEEDBYTES];
        randombytes_buf(seed, sizeof seed);
        crypto_sign_seed_keypair(pk, sk, seed);
        std::ofstream(std::string(argv[2]) + "/update_private.key") << b64(seed, sizeof seed);
        std::ofstream(std::string(argv[2]) + "/update_public.key") << b64(pk, sizeof pk);
        std::puts("keys written");
        return 0;
    }
    if (argc >= 4 && !std::strcmp(argv[1], "sign")) {
        std::vector<unsigned char> seed;
        if (!unb64(readText(argv[2]), seed, 64) || seed.size() != crypto_sign_SEEDBYTES) { std::fputs("bad private key\n", stderr); return 1; }
        unsigned char pk[crypto_sign_PUBLICKEYBYTES], sk[crypto_sign_SECRETKEYBYTES], sig[crypto_sign_BYTES];
        crypto_sign_seed_keypair(pk, sk, seed.data());
        const auto data = readBin(argv[3]);
        crypto_sign_detached(sig, nullptr, data.data(), data.size(), sk);
        std::puts(b64(sig, sizeof sig).c_str());
        return 0;
    }
    if (argc >= 5 && !std::strcmp(argv[1], "verify")) {
        std::vector<unsigned char> pk, sig;
        if (!unb64(readText(argv[2]), pk, 64) || !unb64(argv[4], sig, 128) || pk.size() != crypto_sign_PUBLICKEYBYTES || sig.size() != crypto_sign_BYTES) return 1;
        const auto data = readBin(argv[3]);
        const bool ok = crypto_sign_verify_detached(sig.data(), data.data(), data.size(), pk.data()) == 0;
        std::puts(ok ? "OK" : "INVALID");
        return ok ? 0 : 1;
    }
    std::fputs("usage: mmsign genkeys <dir> | sign <private.key> <file> | verify <public.key> <file> <sig>\n", stderr);
    return 2;
}
