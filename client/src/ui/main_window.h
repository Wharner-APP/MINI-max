#pragma once
#include <QMainWindow>

#include "core/config.h"
#include "core/context.h"
#include "mm/endpoint.h"

class ApiClient;
class AuthView;
class ChatsModel;
class ConnectionMonitor;
class Engine;
class MainView;
class PopupHost;
class QStackedWidget;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const ClientConfig &cfg, const ServerEndpoint &ep, bool demo, QWidget *parent = nullptr);
    void devOpen(const QString &target);   // development helper: --open <target>
    void devShowAuth(bool registration, bool captcha);
    void devEnterDemo();
protected:
    void closeEvent(QCloseEvent *) override;
private:
    void showAuth();
    void showMain(const Session &s, const QByteArray &localKey, bool demo = false);
    void applyTheme();
    void scheduleSave();
    void saveNow();
    void onTimedOut();
    ClientConfig m_cfg;
    ServerEndpoint m_ep;
    ApiClient *m_api;
    ConnectionMonitor *m_monitor;
    Engine *m_engine = nullptr;
    ChatsModel *m_chats;
    QStackedWidget *m_stack;
    AuthView *m_auth = nullptr;
    MainView *m_main = nullptr;
    PopupHost *m_alerts;
    QTimer *m_saveTimer;
    QByteArray m_localKey;
    bool m_demo;
    bool m_timeoutShown = false;
};
