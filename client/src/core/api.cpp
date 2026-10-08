#include "core/api.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <mm/build_info.h>

ApiClient::ApiClient(const QUrl &base, int timeoutMs, QObject *parent)
    : QObject(parent), m_base(base), m_timeout(timeoutMs) {}

QUrl ApiClient::makeUrl(const QString &path, const QUrlQuery &query) const {
    QUrl u = m_base;
    const QString p = path.startsWith('/') ? path : ("/" + path);
    QString basePath = m_base.path();
    if (basePath.endsWith('/')) basePath.chop(1);
    u.setPath(basePath + p);
    if (!query.isEmpty()) u.setQuery(query);
    return u;
}

QNetworkRequest ApiClient::request(const QUrl &url, bool captchaKey) const {
    QNetworkRequest r(url);
    r.setRawHeader("Accept", "application/json");
    r.setRawHeader("Cache-Control", "no-cache, no-store");
    r.setRawHeader("Pragma", "no-cache");
    r.setRawHeader("User-Agent", QByteArray("MINImax/") + MM_VERSION_STR + " (" + MM_PLATFORM_ID + ")");
    if (!m_token.isEmpty()) r.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    if (captchaKey && qstrlen(MM_CAPTCHA_KEY) > 0) r.setRawHeader("X-Captcha-Key", MM_CAPTCHA_KEY);
    r.setTransferTimeout(m_timeout);
    return r;
}

void ApiClient::track(QNetworkReply *r, QObject *ctx, Callback cb) {
    connect(r, &QNetworkReply::finished, ctx ? ctx : this, [this, r, cb] {
        ApiResult res;
        res.status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.body = r->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(res.body);
        if (doc.isObject()) res.json = doc.object();
        res.ok = r->error() == QNetworkReply::NoError && res.status >= 200 && res.status < 300;
        if (r->error() != QNetworkReply::NoError) resetNetworkConnections();
        if (!res.ok) {
            res.error = res.json.value("message").toString();
            if (res.error.isEmpty()) res.error = res.json.value("error").toString();
            if (res.error.isEmpty()) res.error = r->errorString();
            if (res.error.isEmpty()) res.error = QString("Сетевая ошибка %1").arg(int(r->error()));
        }
        cb(res);
    });
    connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
}

void ApiClient::get(const QString &path, QObject *ctx, Callback cb, bool captchaKey) {
    track(m_nam.get(request(makeUrl(path), captchaKey)), ctx, std::move(cb));
}

void ApiClient::get(const QString &path, const QUrlQuery &query, QObject *ctx, Callback cb, bool captchaKey) {
    track(m_nam.get(request(makeUrl(path, query), captchaKey)), ctx, std::move(cb));
}

void ApiClient::getLong(const QString &path, const QUrlQuery &query, int timeoutMs, QObject *ctx, Callback cb) {
    QNetworkRequest r = request(makeUrl(path, query), false);
    r.setTransferTimeout(timeoutMs);
    track(m_nam.get(r), ctx, std::move(cb));
}

void ApiClient::post(const QString &path, const QJsonObject &body, QObject *ctx, Callback cb, bool captchaKey) {
    QNetworkRequest r = request(makeUrl(path), captchaKey);
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    track(m_nam.post(r, QJsonDocument(body).toJson(QJsonDocument::Compact)), ctx, std::move(cb));
}

void ApiClient::put(const QString &path, const QUrlQuery &query, const QByteArray &body, const QString &contentType,
                    QObject *ctx, Callback cb) {
    QNetworkRequest r = request(makeUrl(path, query), false);
    r.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    track(m_nam.put(r, body), ctx, std::move(cb));
}

void ApiClient::upload(const QString &pathWithQuery, const QByteArray &data, const QString &contentType,
                       QObject *ctx, Callback cb) {
    QString path = pathWithQuery;
    QUrlQuery q;
    const int qi = path.indexOf('?');
    if (qi >= 0) {
        q = QUrlQuery(path.mid(qi + 1));
        path = path.left(qi);
    }
    QNetworkRequest r = request(makeUrl(path, q), false);
    r.setHeader(QNetworkRequest::ContentTypeHeader, contentType.isEmpty() ? "application/octet-stream" : contentType);
    r.setHeader(QNetworkRequest::ContentLengthHeader, data.size());
    const int extra = 30000 + int(data.size() / 1024);
    r.setTransferTimeout(qMax(m_timeout, qMax(60000, extra)));
    track(m_nam.post(r, data), ctx, std::move(cb));
}

void ApiClient::download(const QString &urlOrPath, QObject *ctx, std::function<void(const QByteArray &, const QString &)> cb) {
    QUrl u(urlOrPath);
    if (u.scheme().isEmpty())
        u = makeUrl(urlOrPath.startsWith('/') ? urlOrPath : ("/" + urlOrPath));
    QNetworkRequest rq = (u.host().isEmpty() || u.host() == m_base.host()) ? request(u, false) : QNetworkRequest(u);
    rq.setRawHeader("Accept", "*/*");
    rq.setRawHeader("User-Agent", QByteArray("MINImax/") + MM_VERSION_STR + " (" + MM_PLATFORM_ID + ")");
    rq.setTransferTimeout(qMax(m_timeout, 30000));
    QNetworkReply *r = m_nam.get(rq);
    connect(r, &QNetworkReply::finished, ctx ? ctx : this, [r, cb] {
        const bool ok = r->error() == QNetworkReply::NoError;
        cb(ok ? r->readAll() : QByteArray(), ok ? QString() : r->errorString());
    });
    connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
}
