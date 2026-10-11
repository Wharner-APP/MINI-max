#include "core/payments.h"

#include <QJsonObject>
#include <QTimer>

#include "core/api.h"

const char *const kPaymentUnavailable = "Не удалось получить API платёжной системы. Пожалуйста, попробуйте позже";

namespace {
class UnconfiguredProvider : public IPaymentProvider {
public:
    QString id() const override { return "none"; }
    void createPayment(const PaymentRequest &, QObject *ctx, std::function<void(const PaymentResult &)> cb) override {
        QTimer::singleShot(400, ctx, [cb] { PaymentResult r; r.error = QString::fromUtf8(kPaymentUnavailable); cb(r); });
    }
};

class ServerPaymentProvider : public IPaymentProvider {
public:
    explicit ServerPaymentProvider(ApiClient *api) : m_api(api) {}
    QString id() const override { return "server"; }
    void createPayment(const PaymentRequest &q, QObject *ctx, std::function<void(const PaymentResult &)> cb) override {
        QJsonObject body{{"kind", q.kind}, {"item", q.item}, {"amount_minor", static_cast<double>(q.amountMinor)},
                         {"currency", q.currency}, {"quantity", q.quantity}, {"recipient", q.recipient}};
        m_api->post("/api/payments/create", body, ctx, [cb](const ApiResult &r) {
            PaymentResult out;
            const QUrl url(r.json.value("checkout_url").toString());
            if (!r.ok || !url.isValid() || url.isEmpty()) { out.error = QString::fromUtf8(kPaymentUnavailable); cb(out); return; }
            out.ok = true;
            out.paymentId = r.json.value("payment_id").toString();
            out.checkoutUrl = url;
            cb(out);
        });
    }
private:
    ApiClient *m_api;
};
}  // namespace

PaymentManager &PaymentManager::instance() {
    static PaymentManager m;
    return m;
}

void PaymentManager::configure(const QString &providerId, ApiClient *api) {
    if (providerId == "server") m_provider = std::make_unique<ServerPaymentProvider>(api);
    else m_provider = std::make_unique<UnconfiguredProvider>();
}

void PaymentManager::pay(const PaymentRequest &req, QObject *ctx, std::function<void(const PaymentResult &)> cb) {
    if (!m_provider) m_provider = std::make_unique<UnconfiguredProvider>();
    m_provider->createPayment(req, ctx, std::move(cb));
}
