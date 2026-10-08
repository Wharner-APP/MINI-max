#include "ui/main_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QIcon>
#include <QStackedWidget>
#include <QTimer>
#include <mm/build_info.h>
#include <mm/paths.h>

#include "core/api.h"
#include "core/chats_model.h"
#include "core/connection_monitor.h"
#include "core/demo_data.h"
#include "core/payments.h"
#include "core/prefs.h"
#include "core/session_store.h"
#include "core/engine.h"
#include "core/crypto.h"
#include "ui/auth_view.h"
#include "ui/main_view.h"
#include "ui/pages.h"
#include "ui/popup.h"
#include "ui/theme.h"

MainWindow::MainWindow(const ClientConfig &cfg, const ServerEndpoint &ep, bool demo, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg), m_ep(ep), m_demo(demo) {
    setWindowTitle(MM_APP_NAME);
    setMinimumSize(420, 560);
    resize(1100, 720);
    QIcon icon(":/logo.ico");
    if (!icon.isNull()) setWindowIcon(icon);

    m_api = new ApiClient(ep.base, cfg.requestTimeoutSec * 1000, this);
    m_chats = new ChatsModel(this);
    AppContext &ctx = AppContext::i();
    ctx.api = m_api;
    ctx.chats = m_chats;
    ctx.dataDir = MmPaths::dataDir(cfg.dataDir);
    ctx.instructionsUrl = cfg.instructionsUrl;
    PaymentManager::instance().configure(cfg.paymentProvider, m_api);

    m_stack = new QStackedWidget;
    setCentralWidget(m_stack);
    m_alerts = new PopupHost(this);

    m_monitor = new ConnectionMonitor(m_api, cfg.pingIntervalSec, cfg.connectTimeoutSec, this);
    ctx.monitor = m_monitor;
    connect(m_monitor, &ConnectionMonitor::onlineChanged, this, [this](bool on) {
        if (m_auth) m_auth->setStatus(on ? "Подключено к серверу " + m_monitor->serverName() : "Соединение…", on);
        if (m_main) m_main->setOnline(on);
    });
    connect(m_monitor, &ConnectionMonitor::timedOut, this, &MainWindow::onTimedOut);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1500);
    connect(m_saveTimer, &QTimer::timeout, this, &MainWindow::saveNow);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, &MainWindow::applyTheme);

    ctx.logout = [this] {
        saveNow();
        if (m_engine) m_engine->stop();
        SessionStore::clear(AppContext::i().dataDir);
        AppContext::i().session = {};
        AppContext::i().engine = nullptr;
        m_api->setToken({});
        showAuth();
    };

    if (!demo) m_monitor->start();
    applyTheme();

    Session s;
    QByteArray key;
    if (demo) devEnterDemo();
    else if (SessionStore::load(ctx.dataDir, &s, &key)) showMain(s, key);
    else showAuth();
}

void MainWindow::applyTheme() {
    qApp->setStyleSheet(ThemeManager::instance().styleSheet());
    if (m_main) m_main->refreshTheme();
}

void MainWindow::showAuth() {
    if (m_main) { m_stack->removeWidget(m_main); m_main->deleteLater(); m_main = nullptr; }
    m_chats->clearAll();
    m_auth = new AuthView;
    m_stack->addWidget(m_auth);
    m_stack->setCurrentWidget(m_auth);
    m_auth->setStatus(m_monitor->online() ? "Подключено к серверу " + m_monitor->serverName() : "Подключение к серверу…", m_monitor->online());
    connect(m_auth, &AuthView::authenticated, this, [this](const Session &s, const QByteArray &key) {
        SessionStore::save(AppContext::i().dataDir, s, key);
        showMain(s, key);
    });
}

void MainWindow::showMain(const Session &s, const QByteArray &localKey, bool demo) {
    AppContext &ctx = AppContext::i();
    ctx.session = s;
    m_api->setToken(s.token);
    m_localKey = localKey;
    if (m_auth) { m_stack->removeWidget(m_auth); m_auth->deleteLater(); m_auth = nullptr; }
    m_chats->clearAll();
    if (demo) {
        DemoData::populate(m_chats);
    } else if (!SessionStore::loadChats(ctx.dataDir, m_chats, localKey) || m_chats->rowCount() == 0) {
        m_chats->clearAll();
        Chat saved;
        saved.title = "Избранное";
        saved.kind = ChatKind::Saved;
        saved.pinned = true;
        m_chats->addChat(saved);
    }
    const int auto_s = Prefs::instance().get("privacy/auto_delete", 0).toInt();
    for (int r = 0; r < m_chats->rowCount(); ++r) {
        const Chat *c = m_chats->chat(r);
        m_chats->purgeOlderThan(r, c->autoDeleteSec > 0 ? c->autoDeleteSec : auto_s);
    }
    m_main = new MainView(m_chats);
    m_stack->addWidget(m_main);
    m_stack->setCurrentWidget(m_main);
    m_main->setOnline(m_monitor->online() || demo);
    if (!demo) {
        connect(m_chats, &ChatsModel::dataChanged, this, &MainWindow::scheduleSave, Qt::UniqueConnection);
        connect(m_chats, &ChatsModel::rowsInserted, this, &MainWindow::scheduleSave, Qt::UniqueConnection);
        connect(m_chats, &ChatsModel::rowsRemoved, this, &MainWindow::scheduleSave, Qt::UniqueConnection);
        connect(m_chats, &ChatsModel::modelReset, this, &MainWindow::scheduleSave, Qt::UniqueConnection);

        // Live engine: sync + messaging + search + avatars
        if (!m_engine) m_engine = new Engine(m_api, m_chats, this);
        AppContext::i().engine = m_engine;
        mmcrypto::KeyPair kp = mmcrypto::generateKeyPair();
        // Prefer a stable identity key sealed in local store later; for now generate per session.
        m_engine->start(s, localKey, kp);
        connect(m_engine, &Engine::notice, this, [this](const QString &title, const QString &text) {
            if (m_main && m_main->popup()) Pages::infoDialog(m_main->popup(), title, text);
            else m_alerts->dialog(title, text, {{"OK", []{}, false}});
        });
        AppContext::i().openChatRow = [this](int row) { if (m_main) m_main->openChat(row); };
    }
    applyTheme();
}

void MainWindow::scheduleSave() { if (!m_demo) m_saveTimer->start(); }

void MainWindow::saveNow() {
    if (m_demo || m_localKey.isEmpty() || !AppContext::i().session.valid()) return;
    SessionStore::saveChats(AppContext::i().dataDir, *m_chats, m_localKey);
}

void MainWindow::closeEvent(QCloseEvent *e) {
    saveNow();
    QMainWindow::closeEvent(e);
}

void MainWindow::onTimedOut() {
    if (m_timeoutShown) return;
    m_timeoutShown = true;
    const QString github = MM_GITHUB;
    const QString support = QStringLiteral("https://wharner-official-app.tilda.ws/help");
    m_alerts->dialog(
        "Не удалось подключиться",
        QString("Возможно, сервер выключен или у вас устаревшая версия файла connect.ip. "
                "Попробуйте обновить файл connect.ip, следуя инструкциям с "
                "<a href='%1'>GitHub</a>, или свяжитесь с нашей "
                "<a href='%2'>службой поддержки</a>.")
            .arg(github.toHtmlEscaped(), support.toHtmlEscaped()),
        {{"OK", [this] { m_timeoutShown = false; }, false}},
        440);
}

void MainWindow::devEnterDemo() {
    Session s;
    s.login = "demo_user";
    s.displayName = "Демо Пользователь";
    s.token = "demo";
    showMain(s, QByteArray(32, 'k'), true);
}

void MainWindow::devShowAuth(bool registration, bool captcha) {
    if (!m_auth) showAuth();
    m_auth->showTab(registration);
    if (captcha) m_auth->startCaptchaForTest();
}

void MainWindow::devOpen(const QString &target) {
    if (m_main) m_main->openDevTarget(target);
}
