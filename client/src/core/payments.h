#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>
#include <memory>

class ApiClient;

// ---- How to plug in a payment system -----------------------------------------------------
// 1. Implement IPaymentProvider (or keep ServerPaymentProvider and add POST /api/payments/create
//    to the server: body = PaymentRequest as JSON, answer = {"payment_id":"..","checkout_url":".."}).
// 2. Set  [payments] provider = "server"  in configs/client.toml  (or register your own provider
//    in PaymentManager::configure()).
// Until a provider answers successfully the UI shows kPaymentUnavailable.
struct PaymentRequest {
    QString kind;        // premium | stars | gift | business
    QString item;        // plan id / star pack / gift id
    qint64 amountMinor = 0;  // price in minor units (kopecks, cents), 0 = unknown/free-form
    QString currency;    // RUB, USD, ...
    int quantity = 1;
    QString recipient;   // login of the gift recipient, empty = self
};
struct PaymentResult {
    bool ok = false;
    QString error;
    QString paymentId;
    QUrl checkoutUrl;    // page the user pays on (opened in the browser)
};

extern const char *const kPaymentUnavailable;

class IPaymentProvider {
public:
    virtual ~IPaymentProvider() = default;
    virtual QString id() const = 0;
    virtual void createPayment(const PaymentRequest &req, QObject *ctx, std::function<void(const PaymentResult &)> cb) = 0;
};

class PaymentManager {
public:
    static PaymentManager &instance();
    void configure(const QString &providerId, ApiClient *api);
    void pay(const PaymentRequest &req, QObject *ctx, std::function<void(const PaymentResult &)> cb);
    void setProvider(std::unique_ptr<IPaymentProvider> p) { m_provider = std::move(p); }
private:
    std::unique_ptr<IPaymentProvider> m_provider;
};
