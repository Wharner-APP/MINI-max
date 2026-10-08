#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <mm/build_info.h>
#include <mm/endpoint.h>
#include <mm/paths.h>

#include "core/config.h"
#include "core/crypto.h"
#include "core/prefs.h"
#include "ui/main_window.h"
#include "ui/theme.h"

int main(int argc, char **argv) {
    QCoreApplication::setOrganizationName("WharnerApp");
    QCoreApplication::setApplicationName("MINImax");
    QCoreApplication::setApplicationVersion(MM_VERSION_STR);
    {   // UI scale must be set before QApplication exists
        QFile f(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/prefs.json");
        if (f.open(QIODevice::ReadOnly)) {
            const int sc = QJsonDocument::fromJson(f.readAll()).object().value("ui/scale").toInt(100);
            if (sc != 100 && sc >= 50 && sc <= 200 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) qputenv("QT_SCALE_FACTOR", QByteArray::number(sc / 100.0));
        }
    }
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false); // allow MINI max to continue in the system tray

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption cfgOpt({"c", "config"}, "Path to client.toml", "file");
    QCommandLineOption demoOpt("demo", "Load sample chats (UI development)");
    QCommandLineOption shotOpt("screenshot", "Save a window screenshot and exit", "file");
    QCommandLineOption openOpt("open", "Open a screen after start (dev)", "name");
    QCommandLineOption authOpt("auth", "Show auth screen: login | register | captcha (dev)", "tab");
    QCommandLineOption quitOpt("quit-after", "Quit after <ms>", "ms");
    QCommandLineOption sizeOpt("size", "Window size WxH", "WxH");
    cli.addOptions({cfgOpt, demoOpt, shotOpt, openOpt, authOpt, quitOpt, sizeOpt});
    cli.process(app);

    const QString configDir = cli.isSet(cfgOpt) ? QFileInfo(cli.value(cfgOpt)).absolutePath() : MmPaths::configDir();
    const QString cfgPath = cli.isSet(cfgOpt) ? cli.value(cfgOpt) : configDir + "/client.toml";
    ClientConfig cfg;
    try {
        cfg = ClientConfig::load(cfgPath);
    } catch (const ConfigError &e) {
        QMessageBox::critical(nullptr, MM_APP_NAME, QString("Ошибка конфигурации (%1):\n%2").arg(cfgPath, e.what()));
        return 1;
    }
    ServerEndpoint ep = loadServerEndpoint(configDir);
    if (!ep.valid()) {
        // Keep running: the "cannot connect" window explains how to fix connect.ip.
        ep = parseServerAddress("127.0.0.1:1");
    }
    if (!mmcrypto::init()) { QMessageBox::critical(nullptr, MM_APP_NAME, "Не удалось инициализировать криптографию."); return 1; }

    Prefs::instance().open(MmPaths::dataDir(cfg.dataDir));
    QFont f = app.font();
    f.setPointSizeF(10.5);
    app.setFont(f);
    ThemeManager::instance().setMode(Prefs::instance().get("ui/theme", cfg.theme).toString());
    const QString accent = Prefs::instance().get("ui/accent").toString();
    if (!accent.isEmpty()) ThemeManager::instance().setAccent(QColor(accent));

    MainWindow w(cfg, ep, cli.isSet(demoOpt));
    if (cli.isSet(sizeOpt)) {
        const QStringList wh = cli.value(sizeOpt).split('x');
        if (wh.size() == 2) w.resize(wh[0].toInt(), wh[1].toInt());
    }
    w.show();
    if (cli.isSet(authOpt)) w.devShowAuth(cli.value(authOpt) != "login", cli.value(authOpt) == "captcha");
    if (cli.isSet(openOpt)) QTimer::singleShot(300, &w, [&] { w.devOpen(cli.value(openOpt)); });

    if (cli.isSet(shotOpt)) {
        QTimer::singleShot(cli.isSet(authOpt) ? 1800 : 1400, &app, [&] { w.grab().save(cli.value(shotOpt)); app.quit(); });
    } else if (cli.isSet(quitOpt)) {
        QTimer::singleShot(cli.value(quitOpt).toInt(), &app, &QApplication::quit);
    }
    return app.exec();
}
