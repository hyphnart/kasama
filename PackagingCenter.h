#pragma once
#include <QtWidgets>
#include <QtSql>
#include <climits>

// V4.0: Multiple immutable pack definitions; receipts expand to integral base units.
class PackagingCenter final : public QDialog {
    QSqlDatabase database;
    QComboBox *product=nullptr,*warehouse=nullptr,*pack=nullptr;
    QLineEdit *label=nullptr,*barcode=nullptr;
    QSpinBox *units=nullptr,*count=nullptr,*salePrice=nullptr;
    QTableWidget *table=nullptr;
    QLabel *preview=nullptr;
    bool execSql(const QString &sql,const QVariantList &args={},QString *error=nullptr){
        QSqlQuery q(database);
        if(!q.prepare(sql)){if(error)*error=q.lastError().text();return false;}
        for(const QVariant &v:args)q.addBindValue(v);
        if(!q.exec()){if(error)*error=q.lastError().text();return false;}
        return true;
    }
    void reload(){
        const int current=product->currentData().toInt();
        product->clear(); QSqlQuery p(database);p.exec("SELECT id,name,sku FROM products ORDER BY name LIMIT 10000");
        while(p.next())product->addItem(p.value(1).toString()+" ["+p.value(2).toString()+"]",p.value(0));
        const int idx=product->findData(current);if(idx>=0)product->setCurrentIndex(idx);
        reloadPacks();
    }
    void reloadPacks(){
        const int previous=pack->currentData().toInt(); pack->clear();table->setRowCount(0);
        const int id=product->currentData().toInt();if(id<=0)return;
        QSqlQuery q(database);q.prepare("SELECT id,label,units_per_pack,barcode,sale_price_cents FROM product_packs WHERE product_id=? ORDER BY units_per_pack,label");q.addBindValue(id);
        if(!q.exec())return;
        while(q.next()){
            pack->addItem(q.value(1).toString()+" × "+q.value(2).toString(),q.value(0));
            const int r=table->rowCount();table->insertRow(r);
            for(int c=0;c<4;++c){QString v;
                if(c==0)v=q.value(1).toString();if(c==1)v=q.value(2).toString();
                if(c==2)v=q.value(3).toString();if(c==3)v=QString::number(q.value(4).toLongLong()/100.0,'f',2);
                table->setItem(r,c,new QTableWidgetItem(v));
            }
        }
        const int idx=pack->findData(previous);if(idx>=0)pack->setCurrentIndex(idx);
        updatePreview();
    }
    void updatePreview(){
        QSqlQuery q(database);q.prepare("SELECT units_per_pack FROM product_packs WHERE id=? AND product_id=?");
        q.addBindValue(pack->currentData());q.addBindValue(product->currentData());
        const bool ok=q.exec() && q.next();
        const qint64 per=ok?q.value(0).toLongLong():0;
        const qint64 total=per*count->value();
        preview->setText(ok?QString("%1 ambalaj × %2 = %3 ana stok adedi").arg(count->value()).arg(per).arg(total):"Önce ambalaj tanımlayın.");
    }
    void savePack(){
        const int productId=product->currentData().toInt();
        const QString name=label->text().trimmed(), code=barcode->text().trimmed();
        if(productId<=0||name.isEmpty()){QMessageBox::warning(this,"Ambalaj","Ürün ve ambalaj adı zorunlu.");return;}
        if(!code.isEmpty()){
            QSqlQuery existing(database);existing.prepare("SELECT 1 FROM products WHERE (barcode=? OR sku=?) AND id<>? LIMIT 1");existing.addBindValue(code);existing.addBindValue(code);existing.addBindValue(productId);
            if(existing.exec()&&existing.next()){QMessageBox::warning(this,"Barkod","Barkod başka bir üründe kullanılıyor.");return;}
            existing.prepare("SELECT 1 FROM products WHERE (barcode=? OR sku=?) LIMIT 1");existing.addBindValue(code);existing.addBindValue(code);
            if(existing.exec()&&existing.next()){QMessageBox::warning(this,"Barkod","Bu kod bir ürün barkodu veya stok kodu olarak kullanılıyor.");return;}
            existing.prepare("SELECT 1 FROM product_barcodes WHERE barcode=? LIMIT 1");existing.addBindValue(code);
            if(existing.exec()&&existing.next()){QMessageBox::warning(this,"Barkod","Barkod alternatif barkodlarda zaten var.");return;}
        }
        QString error;
        if(!execSql("INSERT INTO product_packs(product_id,label,units_per_pack,barcode,sale_price_cents) VALUES(?,?,?,?,?)",{productId,name,units->value(),code,salePrice->value()},&error)){
            QMessageBox::warning(this,"Kayıt",error);return;
        }
        reloadPacks();
    }
    void receive(){
        const int productId=product->currentData().toInt(), warehouseId=warehouse->currentData().toInt(), packId=pack->currentData().toInt();
        QSqlQuery p(database);p.prepare("SELECT units_per_pack,label FROM product_packs WHERE id=? AND product_id=?");p.addBindValue(packId);p.addBindValue(productId);
        if(!p.exec()||!p.next()){QMessageBox::warning(this,"Mal kabul","Geçerli ambalaj seçin.");return;}
        const qint64 per=p.value(0).toLongLong(),quantity=per*count->value();
        if(quantity<=0||quantity>INT_MAX){QMessageBox::warning(this,"Mal kabul","Miktar desteklenen sınır dışında.");return;}
        if(QMessageBox::question(this,"Mal kabul",QString("Stoğa %1 adet eklensin mi? Bu işlem tedarikçi borcu oluşturmaz.").arg(quantity))!=QMessageBox::Yes)return;
        QString error;
        if(!database.transaction()){QMessageBox::critical(this,"İşlem","Transaction başlatılamadı.");return;}
        QSqlQuery existingStock(database);existingStock.prepare("SELECT qty FROM stock WHERE product_id=? AND warehouse_id=?");
        existingStock.addBindValue(productId);existingStock.addBindValue(warehouseId);
        if(!existingStock.exec()||(existingStock.next() && existingStock.value(0).toLongLong()>INT_MAX-quantity)){
            database.rollback();QMessageBox::critical(this,"İşlem","Stok üst sınırı aşılıyor.");return;
        }
        // SQLite upsert keeps reservations and updates only this warehouse's physical stock.
        const bool ok=execSql("INSERT INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,?,?,0) ON CONFLICT(product_id,warehouse_id) DO UPDATE SET qty=stock.qty+excluded.qty WHERE stock.qty<=2147483647-excluded.qty",{productId,warehouseId,quantity},&error)
          && execSql("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason) VALUES(?,?,?,?)",{productId,warehouseId,quantity,QString("Ambalaj mal kabul #%1: %2 x %3").arg(packId).arg(count->value()).arg(p.value(1).toString())},&error)
          && execSql("INSERT INTO pack_receipts(product_id,warehouse_id,pack_id,pack_name_snapshot,units_snapshot,pack_qty,base_qty) VALUES(?,?,?,?,?,?,?)",{productId,warehouseId,packId,p.value(1).toString(),per,count->value(),quantity},&error)
          && execSql("INSERT INTO carton_stock(warehouse_id,pack_id,sealed_qty,loose_units) VALUES(?,?,?,0) ON CONFLICT(warehouse_id,pack_id) DO UPDATE SET sealed_qty=carton_stock.sealed_qty+excluded.sealed_qty",{warehouseId,packId,count->value()},&error)
          && execSql("INSERT INTO carton_events(product_id,warehouse_id,pack_id,action,case_qty,units_snapshot,loose_delta,note) VALUES(?,?,?,'RECEIVE',?,?,0,?)",{productId,warehouseId,packId,count->value(),per,QStringLiteral("Ambalaj mal kabul")},&error);
        // Verify modified rows and rollback on quantity limits.
        QSqlQuery check(database);check.prepare("SELECT qty FROM stock WHERE product_id=? AND warehouse_id=?");check.addBindValue(productId);check.addBindValue(warehouseId);
        const bool stockOk=check.exec()&&check.next()&&check.value(0).toLongLong()>=quantity;
        if(!ok||!stockOk||!database.commit()){
            database.rollback();QMessageBox::critical(this,"İşlem",error.isEmpty()?"Stok işlemi tamamlanamadı.":error);return;
        }
        QMessageBox::information(this,"Tamamlandı",QString("%1 ana stok adedi kaydedildi.").arg(quantity));
    }
public:
    static bool install(QSqlDatabase db,QString *error=nullptr){
        QSqlQuery q(db);
        const QStringList sql={
          "CREATE TABLE IF NOT EXISTS product_packs(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL REFERENCES products(id) ON DELETE CASCADE,label TEXT NOT NULL,units_per_pack INTEGER NOT NULL CHECK(units_per_pack BETWEEN 1 AND 100000),barcode TEXT NOT NULL DEFAULT '',sale_price_cents INTEGER NOT NULL DEFAULT 0 CHECK(sale_price_cents>=0),created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,UNIQUE(product_id,label,units_per_pack))",
          "CREATE UNIQUE INDEX IF NOT EXISTS ux_pack_barcode ON product_packs(barcode) WHERE barcode<>''",
          "CREATE TABLE IF NOT EXISTS pack_receipts(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),pack_id INTEGER REFERENCES product_packs(id) ON DELETE SET NULL,pack_name_snapshot TEXT NOT NULL,units_snapshot INTEGER NOT NULL CHECK(units_snapshot>0),pack_qty INTEGER NOT NULL CHECK(pack_qty>0),base_qty INTEGER NOT NULL CHECK(base_qty>0),created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"};
        for(const auto &s:sql)if(!q.exec(s)){if(error)*error=q.lastError().text();return false;}return true;
    }
    explicit PackagingCenter(QSqlDatabase d,QWidget *parent=nullptr):QDialog(parent),database(d){
        setWindowTitle("KasaPro · Ambalaj / Koli Merkezi");resize(900,690);setMinimumSize(680,550);
        auto *root=new QVBoxLayout(this);auto *title=new QLabel("Ambalaj ve Koli Yönetimi");title->setStyleSheet("font-size:22px;font-weight:700");root->addWidget(title);
        root->addWidget(new QLabel("Bir ürün için 12'li, 24'lü koli veya paket tanımlayın. Stoklar tek ana birimde saklanır."));
        auto *choices=new QFormLayout;product=new QComboBox;warehouse=new QComboBox;choices->addRow("Ürün",product);choices->addRow("Depo",warehouse);root->addLayout(choices);
        QSqlQuery w(database);w.exec("SELECT id,name FROM warehouses ORDER BY name");while(w.next())warehouse->addItem(w.value(1).toString(),w.value(0));
        auto *form=new QGroupBox("Yeni ambalaj tipi");auto *f=new QGridLayout(form);
        label=new QLineEdit;label->setPlaceholderText("Örn: 24'lü koli");units=new QSpinBox;units->setRange(1,100000);units->setValue(24);
        barcode=new QLineEdit;barcode->setPlaceholderText("İsteğe bağlı ayrı barkod");salePrice=new QSpinBox;salePrice->setRange(0,100000000);salePrice->setSuffix(" kuruş");
        f->addWidget(new QLabel("Ad"),0,0);f->addWidget(label,0,1);f->addWidget(new QLabel("Koli içi ana adet"),0,2);f->addWidget(units,0,3);
        f->addWidget(new QLabel("Barkod"),1,0);f->addWidget(barcode,1,1);f->addWidget(new QLabel("Koli satış fiyatı"),1,2);f->addWidget(salePrice,1,3);
        auto *save=new QPushButton("Ambalaj Tipini Kaydet");f->addWidget(save,2,2,1,2);root->addWidget(form);
        table=new QTableWidget(0,4);table->setHorizontalHeaderLabels({"Ambalaj","İçerik (adet)","Barkod","Fiyat (TL)"});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table->setEditTriggers(QAbstractItemView::NoEditTriggers);root->addWidget(table,1);
        auto *receipt=new QGroupBox("Koliyle mal kabul (cari borç oluşturmaz)");auto *r=new QHBoxLayout(receipt);pack=new QComboBox;count=new QSpinBox;count->setRange(1,100000);count->setValue(1);preview=new QLabel;
        r->addWidget(new QLabel("Ambalaj"));r->addWidget(pack,2);r->addWidget(new QLabel("Koli"));r->addWidget(count);r->addWidget(preview,2);
        auto *receiveButton=new QPushButton("Stoğa Ekle");r->addWidget(receiveButton);root->addWidget(receipt);
        connect(product,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{reloadPacks();});
        connect(pack,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{updatePreview();});
        connect(count,QOverload<int>::of(&QSpinBox::valueChanged),this,[this]{updatePreview();});
        connect(save,&QPushButton::clicked,this,[this]{savePack();});connect(receiveButton,&QPushButton::clicked,this,[this]{receive();});
        reload();
    }
};
