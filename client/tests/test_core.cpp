#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "core/chats_model.h"
#include "core/config.h"
#include "core/crypto.h"
#include "core/payments.h"
#include "mm/endpoint.h"
#include "mm/manifest.h"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    CHECK(argc > 1);

    // connect.ip parsing
    CHECK(parseServerAddress("bore.pub:12345").base.toString() == "http://bore.pub:12345");
    CHECK(parseServerAddress("# comment\n\n  https://srv.example.com/ \n").base.toString() == "https://srv.example.com");
    CHECK(parseServerAddress("192.168.1.5").base.toString() == "http://192.168.1.5");
    CHECK(parseServerAddress("http://10.0.0.1:8080/api/").base.toString() == "http://10.0.0.1:8080/api");
    CHECK(!parseServerAddress("# only comments\n").valid());
    CHECK(!parseServerAddress("ftp://x.y").valid());

    // manifest safety
    CHECK(isSafeRelativePath("minimax.app/Contents/Info.plist"));
    CHECK(!isSafeRelativePath("../evil")); CHECK(!isSafeRelativePath("/etc/passwd"));
    CHECK(!isSafeRelativePath("a/../b")); CHECK(!isSafeRelativePath("C:/x")); CHECK(!isSafeRelativePath("a\\b"));
    const QByteArray good = R"({"platform":"p","revision":"r1","files":[{"path":"a/b.txt","sha256":"0000000000000000000000000000000000000000000000000000000000000000","size":1}],"removed":["old.dll"]})";
    CHECK(Manifest::parse(good).valid);
    CHECK(!Manifest::parse(R"({"revision":"r","files":[{"path":"../x","sha256":"00"}]})").valid);
    CHECK(!Manifest::parse("nope").valid);
    CHECK(isProtectedPath("configs/connect.ip"));

    // crypto: signature, key derivation, E2E sealing
    CHECK(mmcrypto::init());
    const auto a = mmcrypto::generateKeyPair(), b = mmcrypto::generateKeyPair();
    const QByteArray k1 = mmcrypto::sharedKey(a.priv, a.pub, b.pub), k2 = mmcrypto::sharedKey(b.priv, b.pub, a.pub);
    CHECK(k1.size() == 32 && k1 == k2);
    const QByteArray blob = mmcrypto::seal(k1, "привет, мир");
    bool ok = false;
    CHECK(mmcrypto::open(k2, blob, &ok) == "привет, мир" && ok);
    QByteArray tampered = blob; tampered.data()[tampered.size() - 1] ^= 1;
    mmcrypto::open(k2, tampered, &ok); CHECK(!ok);
    mmcrypto::open(mmcrypto::generateKeyPair().priv, blob, &ok); CHECK(!ok);
    const auto d1 = mmcrypto::deriveFromPassword("Alice", "secret"), d2 = mmcrypto::deriveFromPassword("alice ", "secret"),
               d3 = mmcrypto::deriveFromPassword("alice", "other");
    CHECK(d1.authKey == d2.authKey && d1.localKey == d2.localKey);
    CHECK(d1.authKey != d3.authKey && d1.authKey != d1.localKey);

    // chats persistence + auto delete
    ChatsModel m;
    const int r = m.addChat("Saved", ChatKind::Saved);
    m.appendMessage(r, "one", true);
    ChatsModel m2; m2.fromJson(m.toJson());
    CHECK(m2.rowCount() == 1 && m2.chat(0)->messages.size() == 1 && m2.chat(0)->messages[0].text == "one");
    m.chat(r)->messages[0].time = QDateTime::currentDateTime().addDays(-3);
    m.purgeOlderThan(r, 86400);
    CHECK(m.chat(r)->messages.isEmpty());

    // config
    ClientConfig c = ClientConfig::load(argv[1]);
    CHECK(c.connectTimeoutSec == 90 && !c.instructionsUrl.isEmpty());
    QTemporaryDir dir; QFile f(dir.filePath("bad.toml")); CHECK(f.open(QIODevice::WriteOnly)); f.write("[network]\n"); f.close();
    bool threw = false; try { ClientConfig::load(f.fileName()); } catch (const ConfigError &) { threw = true; }
    CHECK(threw);
    std::puts("core: OK");
    return 0;
}
