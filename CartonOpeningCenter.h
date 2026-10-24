#pragma once
#include <QtWidgets>
#include <QtSql>
#include <climits>

// Physical carton accounting is separate from total base-unit inventory.
// Opening transfers sealed packaging into loose units WITHOUT changing stock.qty.
// The existing POS checkout still only updates stock.qty; do not use this as a
// complete automatic-unpacking system until every stock mutation is integrated.
class CartonOpeningCenter final : public QDialog {
    QSqlDatabase db;
    QComboBox *product=nullptr,*warehouse=nullptr,*pack=nullptr,*policy=nullptr;
    QSpinBox *count=nullptr,*neededUnits=nullptr;
    QLabel *overview=nullptr;
    QTableWidget *history=nullptr;
    static bool exec(QSqlDatabase &db,const QString &sql,const QVariantList &params={},QString *err=nullptr){
        QSqlQuery q(db);if(!q.prepare(sql)){if(err)*err=q.lastError().text();return false;}
        for(const auto &p:params)q.addBindValue(p);
        if(!q.exec()){if(err)*err=q.lastError().text();return false;}return true;
    }
    void loadPacks(){
        pack->clear(); QSqlQuery q(db);q.prepare("SELECT id,label,units_per_pack FROM product_packs WHERE product_id=? ORDER BY units_per_pack");
        q.addBindValue(product->currentData());if(q.exec())while(q.next())pack->addItem(QString("%1 (%2 adet)").arg(q.value(1).toString()).arg(q.value(2).toString()),q.value(0));
        refresh();
    }
    void refresh(){
        int productId=product->currentData().toInt(),warehouseId=warehouse->currentData().toInt(),packId=pack->currentData().toInt();
        QSqlQuery q(db);q.prepare("SELECT COALESCE(s.qty,0),COALESCE(s.reserved,0),COALESCE(c.sealed_qty,0),COALESCE(c.loose_units,0),COALESCE(p.units_per_pack,0) FROM product_packs p LEFT JOIN stock s ON s.product_id=p.product_id AND s.warehouse_id=? LEFT JOIN carton_stock c ON c.pack_id=p.id AND c.warehouse_id=? WHERE p.id=? AND p.product_id=?");
        for(const auto &v:QVariantList{warehouseId,warehouseId,packId,productId})q.addBindValue(v);
        if(!q.exec()||!q.next()){overview->setText("Ürün, depo ve ambalaj seçin.");history->setRowCount(0);return;}
        auto qty=q.value(0).toLongLong(),reserved=q.value(1).toLongLong(),sealed=q.value(2).toLongLong(),loose=q.value(3).toLongLong(),per=q.value(4).toLongLong();
        QString warning=sealed*per+loose>qty?"\nUYARI: Fiziksel ambalaj kayıtları toplam stoktan büyük; önce mutabakat yapın.":"";
        overview->setText(QString("Toplam stok: %1 adet | Rezerve: %2 | Kapalı: %3 koli (%4 adet) | Açılmış elde: %5 adet%6")
          .arg(qty).arg(reserved).arg(sealed).arg(sealed*per).arg(loose).arg(warning));
        QSqlQuery h(db);h.prepare("SELECT action,case_qty,units_snapshot,loose_delta,created_at,note FROM carton_events WHERE product_id=? AND warehouse_id=? AND pack_id=? ORDER BY id DESC LIMIT 100");
        h.addBindValue(productId);h.addBindValue(warehouseId);h.addBindValue(packId);history->setRowCount(0);
        if(h.exec())while(h.next()) {int r=history->rowCount();history->insertRow(r);for(int c=0;c<6;++c)history->setItem(r,c,new QTableWidgetItem(h.value(c).toString()));}
    }
    void openCases(){
        const int pid=product->currentData().toInt(),wid=warehouse->currentData().toInt(),packId=pack->currentData().toInt(),n=count->value();
        if(pid<=0||wid<=0||packId<=0||n<=0)return;
        if(policy->currentData().toString()=="forbid"){QMessageBox::warning(this,"Ambalaj","Bu ambalajın açılması yasak.");return;}
        QSqlQuery p(db);p.prepare("SELECT units_per_pack FROM product_packs WHERE id=? AND product_id=?");p.addBindValue(packId);p.addBindValue(pid);
        if(!p.exec()||!p.next())return;
        const qint64 per=p.value(0).toLongLong();if(per<=0||per>INT_MAX/n)return;
        if(QMessageBox::question(this,"Koli açma",QString("%1 kapalı koli açılacak, %2 adet açık stoğa dönüşecek. Toplam stok değişmez. Onaylıyor musunuz?").arg(n).arg(n*per))!=QMessageBox::Yes)return;
        QString error;if(!db.transaction()){QMessageBox::warning(this,"Veritabanı",db.lastError().text());return;}
        QSqlQuery s(db);s.prepare("SELECT qty,reserved FROM stock WHERE product_id=? AND warehouse_id=?");s.addBindValue(pid);s.addBindValue(wid);
        if(!s.exec()||!s.next()){db.rollback();QMessageBox::warning(this,"Stok","Stok kaydı yok.");return;}
        const qint64 total=s.value(0).toLongLong(),reserved=s.value(1).toLongLong();
        QSqlQuery c(db);c.prepare("SELECT sealed_qty,loose_units FROM carton_stock WHERE warehouse_id=? AND pack_id=?");c.addBindValue(wid);c.addBindValue(packId);
        if(!c.exec()||!c.next()||c.value(0).toLongLong()<n){db.rollback();QMessageBox::warning(this,"Koli","Yeterli kapalı koli yok.");return;}
        const qint64 sealed=c.value(0).toLongLong(),loose=c.value(1).toLongLong();
        // Existing POS does not consume tracked loose units yet. Fail closed on mismatch.
        if(sealed*per+loose>total||total<reserved){db.rollback();QMessageBox::warning(this,"Mutabakat","Kapalı/açık kayıtları toplam stokla uyuşmuyor.");return;}
        bool ok=exec(db,"UPDATE carton_stock SET sealed_qty=sealed_qty-?,loose_units=loose_units+?,updated_at=CURRENT_TIMESTAMP WHERE warehouse_id=? AND pack_id=? AND sealed_qty>=?",{n,n*per,wid,packId,n},&error);
        QSqlQuery changes(db);ok=ok&&changes.exec("SELECT changes()")&&changes.next()&&changes.value(0).toInt()==1;
        ok=ok&&exec(db,"INSERT INTO carton_events(product_id,warehouse_id,pack_id,action,case_qty,units_snapshot,loose_delta,note) VALUES(?,?,?,'OPEN',?,?,?,?)",{pid,wid,packId,n,per,n*per,QStringLiteral("Manuel koli açma")},&error);
        if(!ok||!db.commit()){db.rollback();QMessageBox::warning(this,"İşlem",error.isEmpty()?"Koli açılamadı.":error);return;}
        refresh();QMessageBox::information(this,"Koli açıldı",QString("%1 koli açıldı; toplam ürün adedi değişmedi.").arg(n));
    }
public:
    static bool install(QSqlDatabase db,QString *err=nullptr){
        const QStringList sql={
          "CREATE TABLE IF NOT EXISTS carton_stock(warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),pack_id INTEGER NOT NULL REFERENCES product_packs(id),sealed_qty INTEGER NOT NULL DEFAULT 0 CHECK(sealed_qty>=0),loose_units INTEGER NOT NULL DEFAULT 0 CHECK(loose_units>=0),updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,PRIMARY KEY(warehouse_id,pack_id))",
          "CREATE TABLE IF NOT EXISTS carton_events(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),pack_id INTEGER NOT NULL REFERENCES product_packs(id),action TEXT NOT NULL CHECK(action IN ('RECEIVE','OPEN')),case_qty INTEGER NOT NULL CHECK(case_qty>0),units_snapshot INTEGER NOT NULL CHECK(units_snapshot>0),loose_delta INTEGER NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,note TEXT NOT NULL DEFAULT '')",
          "CREATE TABLE IF NOT EXISTS carton_policies(pack_id INTEGER PRIMARY KEY REFERENCES product_packs(id),opening_policy TEXT NOT NULL DEFAULT 'manual' CHECK(opening_policy IN ('manual','approval','forbid')))"};
        for(const auto &s:sql)if(!exec(db,s,{},err))return false;return true;
    }
    // One-action helper: calculates the smallest required number of sealed cases,
    // but deliberately does not charge, sell, or modify base-unit inventory.
    void prepareForLooseDemand(){
        const int pid=product->currentData().toInt(),wid=warehouse->currentData().toInt(),packId=pack->currentData().toInt();
        if(pid<=0||wid<=0||packId<=0)return;
        const QString rule=policy->currentData().toString();
        if(rule=="forbid"){QMessageBox::information(this,"Koli açılamaz","Bu ambalajın açılması yasak.");return;}
        if(rule=="approval"){QMessageBox::information(this,"Onay gerekli","Yönetici yetkilendirmesi henüz bağlı değil. Koli açma engellendi.");return;}
        QSqlQuery q(db);q.prepare("SELECT p.units_per_pack,COALESCE(c.sealed_qty,0),COALESCE(c.loose_units,0),COALESCE(s.qty,0),COALESCE(s.reserved,0) FROM product_packs p LEFT JOIN carton_stock c ON c.pack_id=p.id AND c.warehouse_id=? LEFT JOIN stock s ON s.product_id=p.product_id AND s.warehouse_id=? WHERE p.id=? AND p.product_id=?");
        for(const auto &v:QVariantList{wid,wid,packId,pid})q.addBindValue(v);
        if(!q.exec()||!q.next()){QMessageBox::warning(this,"Stok","Seçili ürünün ambalaj kaydı bulunamadı.");return;}
        const qint64 per=q.value(0).toLongLong(),sealed=q.value(1).toLongLong(),loose=q.value(2).toLongLong(),total=q.value(3).toLongLong(),reserved=q.value(4).toLongLong();
        const qint64 demand=neededUnits->value();
        if(per<=0||total<reserved||sealed>LLONG_MAX/per||sealed*per+loose>total){QMessageBox::warning(this,"Mutabakat","Ambalaj veya stok bilgileri tutarsız. Önce stok kayıtlarını kontrol edin.");return;}
        if(demand>total-reserved){QMessageBox::warning(this,"Yetersiz stok",QString("Kullanılabilir toplam %1 adet. Gereken %2 adet.").arg(total-reserved).arg(demand));return;}
        if(demand<=loose){QMessageBox::information(this,"Hazır","Açık adet yeterli. Koli açılması gerekmiyor.");return;}
        const qint64 deficit=demand-loose;
        const qint64 cases=1+(deficit-1)/per;
        if(cases>sealed||cases>INT_MAX){QMessageBox::warning(this,"Kapalı koli yetersiz","Bu ambalaj türünde yeterli kapalı koli yok. Başka ambalajı seçin veya mutabakat yapın.");return;}
        count->setValue(static_cast<int>(cases));
        openCases();
    }
    explicit CartonOpeningCenter(QSqlDatabase database,QWidget *parent=nullptr):QDialog(parent),db(database){
        setWindowTitle("KasaPro | Kolay Koli Yönetimi");resize(1050,710);
        auto *root=new QVBoxLayout(this);auto *title=new QLabel("Kapalı Koli / Açık Adet Defteri");title->setStyleSheet("font-size:21px;font-weight:bold");root->addWidget(title);
        auto *warning=new QLabel("Kolay kullanım: ürün ve ambalajı seçin. Tek koli açabilir veya gereken açık adet miktarını yazıp kaç koli açılacağını otomatik hesaplatabilirsiniz. Bu işlem satış DEĞİLDİR. POS, iade ve transferler henüz fiziksel açık adet defterine bağlı değildir.");warning->setWordWrap(true);root->addWidget(warning);
        auto *inputs=new QFormLayout;product=new QComboBox;warehouse=new QComboBox;pack=new QComboBox;policy=new QComboBox;count=new QSpinBox;count->setRange(1,10000);
        QSqlQuery q(db);q.exec("SELECT id,name FROM products ORDER BY name");while(q.next())product->addItem(q.value(1).toString(),q.value(0));
        q.exec("SELECT id,name FROM warehouses ORDER BY name");while(q.next())warehouse->addItem(q.value(1).toString(),q.value(0));
        policy->addItem("Manuel açma","manual");policy->addItem("Yönetici onaylı (otomatik henüz yok)","approval");policy->addItem("Açılamaz","forbid");
        inputs->addRow("Ürün",product);inputs->addRow("Depo",warehouse);inputs->addRow("Ambalaj",pack);inputs->addRow("Açılacak koli",count);neededUnits=new QSpinBox;neededUnits->setRange(1,1000000);neededUnits->setValue(1);neededUnits->setToolTip("Satış ihtiyacı için gereken açık adedi girin; koli açma miktarı otomatik hesaplanır.");inputs->addRow("İhtiyaç duyulan açık adet",neededUnits);inputs->addRow("Ambalaj açma kuralı",policy);root->addLayout(inputs);
        overview=new QLabel;overview->setWordWrap(true);root->addWidget(overview);
        auto *buttons=new QHBoxLayout;auto *savePolicy=new QPushButton("Kuralı Kaydet");auto *open=new QPushButton("Seçili Koliyi Aç");auto *quickOpen=new QPushButton("1 Koli Aç");auto *prepare=new QPushButton("İhtiyaç Kadar Koli Aç");buttons->addWidget(savePolicy);buttons->addWidget(quickOpen);buttons->addWidget(prepare);buttons->addWidget(open);buttons->addStretch();root->addLayout(buttons);
        history=new QTableWidget(0,6);history->setHorizontalHeaderLabels({"İşlem","Koli","Koli içi","Açılan adet","Zaman","Not"});history->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);history->setEditTriggers(QAbstractItemView::NoEditTriggers);root->addWidget(history,1);
        connect(product,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{loadPacks();});
        connect(warehouse,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{refresh();});
        connect(pack,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
            QSqlQuery q(db);q.prepare("SELECT opening_policy FROM carton_policies WHERE pack_id=?");q.addBindValue(pack->currentData());
            const QString val=q.exec()&&q.next()?q.value(0).toString():"manual";const int i=policy->findData(val);policy->setCurrentIndex(i<0?0:i);refresh();});
        connect(savePolicy,&QPushButton::clicked,this,[this]{
            if(pack->currentData().toInt()<=0)return;
            QString err;if(!exec(db,"INSERT INTO carton_policies(pack_id,opening_policy) VALUES(?,?) ON CONFLICT(pack_id) DO UPDATE SET opening_policy=excluded.opening_policy",{pack->currentData(),policy->currentData()},&err))QMessageBox::warning(this,"Kural",err);
            else QMessageBox::information(this,"Kural","Ambalaj açma kuralı kaydedildi. 'Yönetici onaylı' kuralının güvenli kimlik doğrulaması henüz POS'a bağlı değil.");
        });
        connect(quickOpen,&QPushButton::clicked,this,[this]{count->setValue(1);if(policy->currentData().toString()=="approval"){QMessageBox::warning(this,"Yönetici onayı","Yetki entegrasyonu tamamlanana kadar koli açılamaz.");return;}openCases();});
        connect(prepare,&QPushButton::clicked,this,[this]{prepareForLooseDemand();});
        connect(open,&QPushButton::clicked,this,[this]{if(policy->currentData().toString()=="approval"){QMessageBox::warning(this,"Yönetici onayı","Bu modda yetki entegrasyonu tamamlanana kadar koli açılamaz.");return;}openCases();});
        loadPacks();
    }
};
