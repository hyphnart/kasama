#pragma once
#include <QtWidgets>
#include <QtSql>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QCryptographicHash>

// Dedicated UI gate. No default PIN; one-time supervisor enrollment is required.
// PBKDF2/HMAC-SHA256 with a random 16-byte salt; this is local access control,
// not a substitute for OS-level protection of the SQLite file.
namespace SupervisorGate {
inline QByteArray derive(const QString &pin,const QByteArray &salt) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
        pin.toUtf8(),salt,160000,32);
}
inline bool initialize(QSqlDatabase &database,QString *error=nullptr) {
    QSqlQuery q(database);
    if(!q.exec("CREATE TABLE IF NOT EXISTS supervisor_credentials (id INTEGER PRIMARY KEY CHECK(id=1),salt BLOB NOT NULL,hash BLOB NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)")) {
        if(error)*error=q.lastError().text(); return false;
    }
    if(!q.exec("CREATE TABLE IF NOT EXISTS approval_audit (id INTEGER PRIMARY KEY,action TEXT NOT NULL,reference_id INTEGER NOT NULL,approved_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,result TEXT NOT NULL)")) {
        if(error)*error=q.lastError().text(); return false;
    }
    return true;
}
inline bool configured(QSqlDatabase &database) {
    QSqlQuery q(database); return q.exec("SELECT 1 FROM supervisor_credentials WHERE id=1") && q.next();
}
inline bool setup(QWidget *parent,QSqlDatabase &database) {
    if(configured(database))return true;
    QDialog dlg(parent);dlg.setWindowTitle("Yönetici Güvenlik Kurulumu");dlg.setMinimumWidth(420);
    QVBoxLayout layout(&dlg);
    QLabel info("İadeleri ve kasa farklarını onaylamak için yönetici PIN'i oluşturun. PIN kaybolursa veritabanı yöneticisi müdahalesi gerekir.");
    info.setWordWrap(true);layout.addWidget(&info);
    QLineEdit pin;pin.setEchoMode(QLineEdit::Password);pin.setPlaceholderText("En az 6 haneli PIN");layout.addWidget(&pin);
    QLineEdit confirm;confirm.setEchoMode(QLineEdit::Password);confirm.setPlaceholderText("PIN tekrar");layout.addWidget(&confirm);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addWidget(&buttons);
    QObject::connect(&buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);
    QObject::connect(&buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
    while(dlg.exec()==QDialog::Accepted){
        if(pin.text().size()<6 || pin.text()!=confirm.text()) {
            QMessageBox::warning(parent,"PIN","PIN en az 6 karakter olmalı ve doğrulama eşleşmeli.");continue;
        }
        QByteArray salt(16,Qt::Uninitialized);
        for(int i=0;i<salt.size();++i) salt[i]=char(QRandomGenerator::system()->generate()&0xFF);
        QSqlQuery q(database);q.prepare("INSERT INTO supervisor_credentials(id,salt,hash) VALUES(1,?,?)");
        q.addBindValue(salt);q.addBindValue(derive(pin.text(),salt));
        if(!q.exec()) {QMessageBox::warning(parent,"Hata","PIN kaydedilemedi: "+q.lastError().text());return false;}
        return true;
    }
    return false;
}
inline bool authorize(QWidget *parent,QSqlDatabase &database,const QString &action,int refId) {
    if(!configured(database) && !setup(parent,database))return false;
    for(int attempt=0;attempt<3;++attempt){
        bool accepted=false;
        QString pin=QInputDialog::getText(parent,"Yönetici Onayı",action+"\nYönetici PIN'i:",QLineEdit::Password,QString(),&accepted);
        if(!accepted)return false;
        QSqlQuery q(database);q.prepare("SELECT salt,hash FROM supervisor_credentials WHERE id=1");
        if(!q.exec()||!q.next())return false;
        const QByteArray candidate=derive(pin,q.value(0).toByteArray());
        const QByteArray stored=q.value(1).toByteArray();
        unsigned char diff=0;
        for(int i=0;i<32;++i) diff|=static_cast<unsigned char>(candidate.at(i)^stored.value(i));
        if(candidate.size()==stored.size()&&diff==0){
            QSqlQuery audit(database);audit.prepare("INSERT INTO approval_audit(action,reference_id,result) VALUES(?,?,'Onaylandi')");
            audit.addBindValue(action);audit.addBindValue(refId);audit.exec();
            return true;
        }
        QMessageBox::warning(parent,"Yönetici Onayı","PIN yanlış. İşlem onaylanmadı.");
    }
    return false;
}
}
