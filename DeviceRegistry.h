#pragma once
#include "PaymentDevice.h"
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QSaveFile>
#include <QDateTime>

// Katalog, entegrasyon başarısı iddiası değildir. Her profil için üretici onayı ve SDK gerekir.
struct DeviceProfile {
    QString id;
    QString brand;
    QString model;
    QString integration;
    bool driverAvailable = false;
};
inline QVector<DeviceProfile> deviceCatalog() {
    return {
        {"demo", "KasaPro", "Simülasyon", "Yerel test; ödeme alınmaz", true},
        {"ingenico-ide280", "Ingenico", "IDE 280", "Üretici GMP-3/SDK", false},
        {"ingenico-iwe280", "Ingenico", "IWE 280", "Üretici GMP-3/SDK", false},
        {"ingenico-move5000f", "Ingenico", "Move 5000 F", "Üretici GMP-3/SDK", false},
        {"pax-a910sf", "PAX", "A910SF", "Üretici onaylı SDK/TSM", false},
        {"hugin-t300", "Hugin", "Tiger T300", "Üretici onaylı entegrasyon", false},
        {"hugin-s1", "Hugin", "S1 Android", "Üretici onaylı entegrasyon", false},
        {"beko-x30tr", "Beko", "X30 TR", "Üretici onaylı entegrasyon", false},
        {"pavo-n86", "Pavo", "N86", "Üretici onaylı entegrasyon", false},
        {"inpos-m530", "inPOS", "m530", "Üretici onaylı entegrasyon", false},
        {"generic-eft", "Banka EFT-POS", "Üreticiye bağlı", "Banka/üretici SDK", false}
    };
}
inline QString describeProfile(const DeviceProfile &p) {
    return p.brand + " " + p.model + " — " + (p.driverAvailable && p.id == "demo" ? "yalnızca simülasyon" : "SDK bekleniyor");
}
class UnavailableDevice final : public IPaymentDevice {
    DeviceProfile profile_;
public:
    explicit UnavailableDevice(const DeviceProfile &p) : profile_(p) {}
    QString displayName() const override {return describeProfile(profile_);}
    DevicePaymentResult startPayment(const DevicePaymentRequest &) override {
        return {PaymentStatus::Error, {}, {}, "Bu model için onaylı sürücü/SDK bağlı değil: " + profile_.brand + " " + profile_.model, false};
    }
    DevicePaymentResult checkStatus(const QString &) override {
        return {PaymentStatus::Unknown, {}, {}, "Gerçek cihazla güvenli durum sorgulaması kullanılamıyor", false};
    }
    bool cancelRequest(const QString &, QString *error) override {
        if(error) *error = "Gerçek cihaz sürücüsü bulunmuyor";
        return false;
    }
};
// Hareket günlüğü, kart sahibi verisi (PAN/CVV/PIN) içermez.
inline bool writeDeviceAudit(const QString &path, const QString &profileId,
                             const DevicePaymentRequest &req, const DevicePaymentResult &result) {
    QFile f(path);
    if(!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return false;
    QJsonObject obj{{"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"profile", profileId}, {"reference", req.merchantReference},
        {"amount_cents", double(req.amountCents)}, {"currency", req.currency},
        {"status", int(result.status)}, {"fiscal_documented", result.fiscallyDocumented}};
    const QByteArray line=QJsonDocument(obj).toJson(QJsonDocument::Compact)+"\n";
    return f.write(line)==line.size();
}
