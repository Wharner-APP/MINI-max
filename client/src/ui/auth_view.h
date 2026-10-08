#pragma once
#include <QWidget>

#include "core/context.h"

class ApiClient;
class PopupHost;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

// Login / registration screen with hCaptcha verification for registration.
class AuthView : public QWidget {
    Q_OBJECT
public:
    explicit AuthView(QWidget *parent = nullptr);
    void setStatus(const QString &text, bool ok);
    void showTab(bool registration);
    PopupHost *popup() const { return m_popup; }
    void startCaptchaForTest();
signals:
    void authenticated(const Session &session, const QByteArray &localKey);
private:
    void setBusy(bool b);
    void showError(const QString &t);
    void submit();
    void doLogin();
    void doRegister();
    void openCaptcha(std::function<void(const QString &id, int x, std::function<void(bool retry)> done)> onSolved);
    void finishAuth(const struct ApiResult &r, const QByteArray &localKey, bool created);
    QString validate() const;

    bool m_register = false;
    QPushButton *m_tabLogin, *m_tabReg, *m_submit;
    QLineEdit *m_login, *m_name, *m_pass, *m_pass2;
    QLabel *m_error, *m_disclosure, *m_status, *m_logo, *m_title, *m_subtitle;
    PopupHost *m_popup;
    QWidget *m_regOnly1, *m_regOnly2;
    struct DerivedPtr;
};
