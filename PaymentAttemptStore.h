#pragma once
#include "PaymentDevice.h"
#include <QtSql>
#include <QString>

// KasaPro cihaz denemeleri: parasal muhasebe kayıtlarından ayrı tutulur.
// Aynı merchant_reference yalnızca bir defa kullanılabilir.
class PaymentAttemptStore {
public:
    static bool migrate(QSqlDatabase &database, QString *error=nullptr) {
        QSqlQuery q(database);
        const QStringList scripts={
            QStringLiteral("CREATE TABLE IF NOT EXISTS device_payment_attempts ("
              "id INTEGER PRIMARY KEY, merchant_reference TEXT NOT NULL UNIQUE,"
              "profile_id TEXT NOT NULL, amount_cents INTEGER NOT NULL CHECK(amount_cents > 0),"
              "currency TEXT NOT NULL DEFAULT 'TRY', status TEXT NOT NULL,"
              "device_reference TEXT NOT NULL DEFAULT '', authorization_code TEXT NOT NULL DEFAULT '',"
              "fiscal_documented INTEGER NOT NULL DEFAULT 0, message TEXT NOT NULL DEFAULT '',"
              "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS ix_device_attempts_status ON device_payment_attempts(status,created_at)")
        };
        for(const QString &sql:scripts){if(!q.exec(sql)){if(error)*error=q.lastError().text();return false;}}
        return true;
    }
    static QString statusName(PaymentStatus status){
        switch(status){
          case PaymentStatus::NotStarted:return QStringLiteral("Baslatilmadi");
          case PaymentStatus::Pending:return QStringLiteral("Bekliyor");
          case PaymentStatus::Approved:return QStringLiteral("Onaylandi");
          case PaymentStatus::Declined:return QStringLiteral("Reddedildi");
          case PaymentStatus::Unknown:return QStringLiteral("Bilinmiyor");
          case PaymentStatus::Error:return QStringLiteral("Hata");
        }
        return QStringLiteral("Bilinmiyor");
    }
    static bool create(QSqlDatabase &database, const DevicePaymentRequest &req,const QString &profile,QString *error=nullptr){
        if(req.merchantReference.isEmpty() || req.amountCents<=0 || req.currency!=QStringLiteral("TRY") || profile.isEmpty()){
            if(error)*error=QStringLiteral("Gecersiz odeme istegi");return false;
        }
        QSqlQuery q(database);
        q.prepare(QStringLiteral("INSERT INTO device_payment_attempts(merchant_reference,profile_id,amount_cents,currency,status) VALUES(?,?,?,?,?)"));
        q.addBindValue(req.merchantReference);q.addBindValue(profile);q.addBindValue(req.amountCents);
        q.addBindValue(req.currency);q.addBindValue(QStringLiteral("Baslatilmadi"));
        if(!q.exec()){if(error)*error=q.lastError().text();return false;}return true;
    }
    static bool update(QSqlDatabase &database,const QString &reference,const DevicePaymentResult &result,QString *error=nullptr){
        QSqlQuery existing(database);
        existing.prepare(QStringLiteral("SELECT status FROM device_payment_attempts WHERE merchant_reference=?"));existing.addBindValue(reference);
        if(!existing.exec() || !existing.next()){if(error)*error=QStringLiteral("Islem referansi bulunamadi");return false;}
        const QString old=existing.value(0).toString();
        if(old==QStringLiteral("Onaylandi") || old==QStringLiteral("Reddedildi")){
            if(error)*error=QStringLiteral("Sonuclanmis odeme degistirilemez");return false;
        }
        QSqlQuery q(database);
        q.prepare(QStringLiteral("UPDATE device_payment_attempts SET status=?,device_reference=?,authorization_code=?,fiscal_documented=?,message=?,updated_at=CURRENT_TIMESTAMP WHERE merchant_reference=?"));
        q.addBindValue(statusName(result.status));q.addBindValue(result.deviceReference);
        q.addBindValue(result.authorizationCode);q.addBindValue(result.fiscallyDocumented?1:0);
        q.addBindValue(result.message.left(2048));q.addBindValue(reference);
        if(!q.exec() || q.numRowsAffected()!=1){if(error)*error=q.lastError().text();return false;}return true;
    }
};
