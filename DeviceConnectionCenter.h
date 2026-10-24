#pragma once
#include "DeviceRegistry.h"
#include <QtWidgets>
#include <QtSql>
#include <QTcpSocket>

// Cihaz profili yönetimi: bağlantı testi ödeme isteği/SDK bağlantısı değildir.
class DeviceConnectionCenter final : public QDialog {
    QSqlDatabase database_;
    QTableWidget *profiles_ = nullptr;
    QTableWidget *exceptions_ = nullptr;
    QLabel *summary_ = nullptr;

    static QString makeError(QSqlQuery &q) { return q.lastError().text(); }
    void initTables() {
        QSqlQuery q(database_);
        if(!q.exec("CREATE TABLE IF NOT EXISTS payment_device_connections("
                   "id INTEGER PRIMARY KEY, profile_id TEXT NOT NULL, display_name TEXT NOT NULL,"
                   "transport TEXT NOT NULL CHECK(transport IN ('TCP','Seri','SDK/TSM')),"
                   "host TEXT NOT NULL DEFAULT '', port INTEGER NOT NULL DEFAULT 0,"
                   "serial_name TEXT NOT NULL DEFAULT '', baud_rate INTEGER NOT NULL DEFAULT 9600,"
                   "enabled INTEGER NOT NULL DEFAULT 0,"
                   "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))
            QMessageBox::critical(this,"Veritabanı",q.lastError().text());
        q.exec("CREATE TABLE IF NOT EXISTS payment_connection_checks("
               "id INTEGER PRIMARY KEY, connection_id INTEGER NOT NULL, outcome TEXT NOT NULL,"
               "description TEXT NOT NULL DEFAULT '',checked_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");
    }
    int chosenId() const {
        if(!profiles_ || profiles_->currentRow()<0 || !profiles_->item(profiles_->currentRow(),0)) return -1;
        return profiles_->item(profiles_->currentRow(),0)->text().toInt();
    }
    void refreshProfiles() {
        QSqlQuery q(database_);
        q.exec("SELECT id,display_name,profile_id,transport,host,port,serial_name,enabled FROM payment_device_connections ORDER BY id DESC");
        profiles_->setRowCount(0);
        while(q.next()) {
            const int row=profiles_->rowCount();profiles_->insertRow(row);
            for(int c=0;c<8;++c){
                QString value=q.value(c).toString();
                if(c==7)value=q.value(c).toInt()?"Etkin":"Pasif";
                profiles_->setItem(row,c,new QTableWidgetItem(value));
            }
        }
        profiles_->resizeColumnsToContents();
    }
    void refreshExceptions() {
        QSqlQuery q(database_);
        if(!q.exec("SELECT created_at,merchant_reference,profile_id,amount_cents,status,message "
                   "FROM device_payment_attempts WHERE status IN ('Bekliyor','Bilinmiyor','Baslatilmadi','Hata') "
                   "ORDER BY id DESC LIMIT 300"))return;
        exceptions_->setRowCount(0);
        qint64 sum=0;
        while(q.next()){
            const int row=exceptions_->rowCount();exceptions_->insertRow(row);
            for(int c=0;c<6;++c){
                QString value=q.value(c).toString();
                if(c==3)value=QLocale(QLocale::Turkish,QLocale::Turkey).toString(q.value(c).toLongLong()/100.0,'f',2)+" TL";
                exceptions_->setItem(row,c,new QTableWidgetItem(value));
            }
            sum+=q.value(3).toLongLong();
        }
        summary_->setText(QString("İncelenecek işlem: %1   •   Tutar toplamı: %2 TL (tahsil edilmiş sayılmaz)")
                           .arg(exceptions_->rowCount())
                           .arg(QLocale(QLocale::Turkish,QLocale::Turkey).toString(sum/100.0,'f',2)));
    }
    void addConnection(){
        QDialog dlg(this);dlg.setWindowTitle("Yeni Cihaz Bağlantısı");dlg.resize(560,430);
        auto *form=new QFormLayout(&dlg);
        auto *name=new QLineEdit;
        auto *model=new QComboBox;
        for(const auto &p:deviceCatalog())model->addItem(p.brand+" "+p.model,p.id);
        auto *transport=new QComboBox;transport->addItems({"TCP","Seri","SDK/TSM"});
        auto *host=new QLineEdit;host->setPlaceholderText("192.168.1.50");
        auto *port=new QSpinBox;port->setRange(0,65535);
        auto *serial=new QLineEdit;serial->setPlaceholderText("COM3");
        auto *baud=new QComboBox;baud->addItems({"9600","19200","38400","57600","115200"});
        auto *enabled=new QCheckBox("Profil etkin (ödeme sürücüsü olduğu anlamına gelmez)");
        form->addRow("Cihaz adı",name);form->addRow("Marka/model",model);form->addRow("Bağlantı",transport);
        form->addRow("IP / adres",host);form->addRow("Port",port);form->addRow("Seri port",serial);
        form->addRow("Baud rate",baud);form->addRow(enabled);
        auto *tip=new QLabel("UYARI: Bu kayıt yalnızca cihaz ayarlarını saklar. Üretici SDK'sı olmadan ödeme gönderilmez.");
        tip->setWordWrap(true);form->addRow(tip);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
        if(dlg.exec()!=QDialog::Accepted)return;
        if(name->text().trimmed().isEmpty()){QMessageBox::warning(this,"Profil","Cihaz adı gerekli.");return;}
        const QString method=transport->currentText();
        if(method=="TCP" && (host->text().trimmed().isEmpty() || port->value()==0)){
            QMessageBox::warning(this,"Profil","TCP için geçerli adres ve port gerekli.");return;
        }
        if(method=="Seri" && serial->text().trimmed().isEmpty()){
            QMessageBox::warning(this,"Profil","Seri port adı gerekli.");return;
        }
        QSqlQuery q(database_);
        q.prepare("INSERT INTO payment_device_connections(profile_id,display_name,transport,host,port,serial_name,baud_rate,enabled) VALUES(?,?,?,?,?,?,?,?)");
        q.addBindValue(model->currentData());q.addBindValue(name->text().trimmed());q.addBindValue(method);
        q.addBindValue(host->text().trimmed());q.addBindValue(port->value());q.addBindValue(serial->text().trimmed());
        q.addBindValue(baud->currentText().toInt());q.addBindValue(enabled->isChecked()?1:0);
        if(!q.exec()){QMessageBox::warning(this,"Kayıt",q.lastError().text());return;}
        refreshProfiles();
    }
    void probeConnection(){
        const int id=chosenId();if(id<0){QMessageBox::information(this,"Seçim","Önce bir cihaz profili seçin.");return;}
        QSqlQuery q(database_);q.prepare("SELECT transport,host,port FROM payment_device_connections WHERE id=?");
        q.addBindValue(id);if(!q.exec() || !q.next())return;
        const QString transport=q.value(0).toString();
        QString outcome="Test edilmedi", detail;
        if(transport=="TCP"){
            QTcpSocket socket;
            socket.connectToHost(q.value(1).toString(),quint16(q.value(2).toUInt()));
            if(socket.waitForConnected(1800)){
                outcome="TCP erişilebilir";
                detail="TCP portu bağlantı kabul etti. Cihaz kimliği/SDK, ödeme ve mali fiş doğrulanmadı.";
                socket.disconnectFromHost();
            }else{
                outcome="TCP erişilemiyor"; detail=socket.errorString();
            }
        }else{
            detail="Bu bağlantı türü için yetkili SDK/Qt Serial Port adaptörü henüz yok. Port açılmadı, ödeme denenmedi.";
        }
        QSqlQuery log(database_);
        log.prepare("INSERT INTO payment_connection_checks(connection_id,outcome,description) VALUES(?,?,?)");
        log.addBindValue(id);log.addBindValue(outcome);log.addBindValue(detail);log.exec();
        QMessageBox::information(this,"Bağlantı kontrolü",outcome+"\n"+detail);
    }
    void deleteConnection(){
        const int id=chosenId();if(id<0)return;
        if(QMessageBox::question(this,"Bağlantıyı sil","Seçili cihaz profilini silmek istiyor musunuz?")!=QMessageBox::Yes)return;
        QSqlQuery q(database_);q.prepare("DELETE FROM payment_device_connections WHERE id=?");q.addBindValue(id);
        if(!q.exec())QMessageBox::warning(this,"Silme",makeError(q));
        refreshProfiles();
    }
public:
    explicit DeviceConnectionCenter(QSqlDatabase database,QWidget *parent=nullptr):QDialog(parent),database_(database){
        setWindowTitle("KasaPro — Ödeme Cihazı Yönetim Merkezi");resize(1120,720);setMinimumSize(800,520);
        initTables();
        auto *root=new QVBoxLayout(this);
        auto *title=new QLabel("Ödeme Cihazları ve Mutabakat");title->setStyleSheet("font-size:22px;font-weight:700;color:#173b52");root->addWidget(title);
        auto *warning=new QLabel("Cihaz profilleri ve TCP erişim testi gerçek banka işlemi değildir. SDK'sı olmayan cihazlarda canlı tahsilat kapalıdır.");
        warning->setWordWrap(true);root->addWidget(warning);
        auto *tabs=new QTabWidget;root->addWidget(tabs);
        auto *connections=new QWidget;auto *cLayout=new QVBoxLayout(connections);
        auto *actions=new QHBoxLayout;
        auto *add=new QPushButton("+ Yeni Cihaz");auto *probe=new QPushButton("TCP Erişim Testi");
        auto *remove=new QPushButton("Profili Sil");auto *refresh=new QPushButton("Yenile");
        actions->addWidget(add);actions->addWidget(probe);actions->addWidget(remove);actions->addStretch();actions->addWidget(refresh);
        cLayout->addLayout(actions);
        profiles_=new QTableWidget;profiles_->setColumnCount(8);
        profiles_->setHorizontalHeaderLabels({"ID","Cihaz Adı","Model","Yöntem","IP","Port","COM","Durum"});
        profiles_->setEditTriggers(QAbstractItemView::NoEditTriggers);profiles_->setSelectionBehavior(QAbstractItemView::SelectRows);
        profiles_->setSelectionMode(QAbstractItemView::SingleSelection);profiles_->horizontalHeader()->setStretchLastSection(true);
        cLayout->addWidget(profiles_);tabs->addTab(connections,"Cihaz Profilleri");
        auto *reconcile=new QWidget;auto *rLayout=new QVBoxLayout(reconcile);
        summary_=new QLabel;rLayout->addWidget(summary_);
        auto *reconcileWarning=new QLabel("Belirsiz ödeme için otomatik tekrar deneme YOK. Banka/üretici sisteminden bağımsız doğrulama yapılmadan satış tamamlamayın.");
        reconcileWarning->setWordWrap(true);rLayout->addWidget(reconcileWarning);
        exceptions_=new QTableWidget;exceptions_->setColumnCount(6);
        exceptions_->setHorizontalHeaderLabels({"Tarih","İşlem Referansı","Cihaz Profili","Tutar","Durum","Mesaj"});
        exceptions_->setEditTriggers(QAbstractItemView::NoEditTriggers);exceptions_->horizontalHeader()->setStretchLastSection(true);
        rLayout->addWidget(exceptions_);
        auto *recheck=new QPushButton("Listeyi Yenile");rLayout->addWidget(recheck);
        tabs->addTab(reconcile,"Mutabakat Bekleyenler");
        connect(add,&QPushButton::clicked,this,[this](){addConnection();});
        connect(probe,&QPushButton::clicked,this,[this](){probeConnection();});
        connect(remove,&QPushButton::clicked,this,[this](){deleteConnection();});
        connect(refresh,&QPushButton::clicked,this,[this](){refreshProfiles();});
        connect(recheck,&QPushButton::clicked,this,[this](){refreshExceptions();});
        connect(tabs,&QTabWidget::currentChanged,this,[this](int index){if(index==1)refreshExceptions();});
        refreshProfiles();refreshExceptions();
    }
};
