#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

struct ApiResult {
    bool ok = false;
    int status = 0;
    QJsonObject json;
    QByteArray body;
    QString error;
};

class ApiClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const ApiResult &)>;
    ApiClient(const QUrl &base, int timeoutMs, QObject *parent = nullptr);
    QUrl base() const { return m_base; }
    void setToken(const QString &t) { m_token = t; }
    QString token() const { return m_token; }

    void get(const QString &path, QObject *ctx, Callback cb, bool captchaKey = false);
    void get(const QString &path, const QUrlQuery &query, QObject *ctx, Callback cb, bool captchaKey = false);
    void post(const QString &path, const QJsonObject &body, QObject *ctx, Callback cb, bool captchaKey = false);
    void put(const QString &path, const QUrlQuery &query, const QByteArray &body, const QString &contentType,
             QObject *ctx, Callback cb);
    void upload(const QString &pathWithQuery, const QByteArray &data, const QString &contentType,
                QObject *ctx, Callback cb);
    void download(const QString &urlOrPath, QObject *ctx, std::function<void(const QByteArray &, const QString &)> cb);
    void getLong(const QString &path, const QUrlQuery &query, int timeoutMs, QObject *ctx, Callback cb);

private:
    QNetworkRequest request(const QUrl &url, bool captchaKey) const;
    void track(QNetworkReply *r, QObject *ctx, Callback cb);
    QUrl makeUrl(const QString &path, const QUrlQuery &query = {}) const;
    QNetworkAccessManager m_nam;
    QUrl m_base;
    int m_timeout;
    QString m_token;
};
