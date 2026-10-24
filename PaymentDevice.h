#pragma once
#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QtGlobal>
#include <memory>

// Üretici SDK adaptörü burada uygulanır. Demo cihazla gerçek tahsilat yapılamaz.
enum class PaymentStatus { NotStarted, Pending, Approved, Declined, Unknown, Error };
struct DevicePaymentRequest {
    QString merchantReference;
    qint64 amountCents = 0;
    QString currency = QStringLiteral("TRY");
};
struct DevicePaymentResult {
    PaymentStatus status = PaymentStatus::NotStarted;
    QString deviceReference;
    QString authorizationCode;
    QString message;
    bool fiscallyDocumented = false;
};
class IPaymentDevice {
public:
    virtual ~IPaymentDevice() = default;
    virtual QString displayName() const = 0;
    virtual DevicePaymentResult startPayment(const DevicePaymentRequest &request) = 0;
    virtual DevicePaymentResult checkStatus(const QString &merchantReference) = 0;
    virtual bool cancelRequest(const QString &merchantReference, QString *error) = 0;
};

// Bilerek ASLA Approved dönmez. Kullanıcılar bir simülatörü gerçek tahsilat sanmamalı.
class DemoPaymentDevice final : public IPaymentDevice {
public:
    QString displayName() const override { return QStringLiteral("Simülasyon — gerçek tahsilat yapmaz"); }
    DevicePaymentResult startPayment(const DevicePaymentRequest &request) override {
        if (request.amountCents <= 0 || request.currency != QStringLiteral("TRY") || request.merchantReference.isEmpty())
            return {PaymentStatus::Error, {}, {}, QStringLiteral("Geçersiz tutar veya işlem referansı"), false};
        return {PaymentStatus::Pending, {}, {}, QStringLiteral("Simülasyon: ödeme isteği hazır; gerçek cihaza gönderilmedi"), false};
    }
    DevicePaymentResult checkStatus(const QString &) override {
        return {PaymentStatus::Unknown, {}, {}, QStringLiteral("Simülasyonda banka onayı ve mali fiş yok"), false};
    }
    bool cancelRequest(const QString &, QString *error) override {
        if (error) *error = QStringLiteral("Simülasyonda iptal edilecek bir banka işlemi yok");
        return false;
    }
};

inline QString createPaymentReference() {
    return QStringLiteral("KP-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
