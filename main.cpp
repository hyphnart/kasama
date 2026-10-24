#include "CartonSaleEngine.h"
#include "CartonOpeningCenter.h"
#include "PackagingCenter.h"
#include "FractionalReconciliationDialog.h"
#include "FractionalTradeDialog.h"
#include "FractionalStockDialog.h"
#include "SectorCatalogDialog.h"
#include "VariantManagerDialog.h"
#include "UiDesign.h"
#include "BarcodeLabelPrinter.h"
#include <QtWidgets>
#include <QtSql>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <initializer_list>
#include <utility>
#include <climits>
#include <QFileInfo>
#include <QUuid>
#include <QPixmap>
#include "SupervisorGate.h"
#include "PaymentDevice.h"
#include "DeviceRegistry.h"
#include "PaymentAttemptStore.h"
#include "DeviceConnectionCenter.h"
#include <QSettings>
#include <QShortcut>
#include <QScreen>
#include <QPrinter>
#include <QPrintDialog>
#include <QTextDocument>
#include <QPageSize>
#include <QPdfWriter>

static QSqlDatabase db;
static bool run(const QString &sql,const QVariantList &args={},QString *error=nullptr){
 QSqlQuery q(db); if(!q.prepare(sql)){if(error)*error=q.lastError().text();return false;}
 for(const auto &a:args)q.addBindValue(a);
 if(!q.exec()){if(error)*error=q.lastError().text();return false;} return true;
}
static QSqlQuery query(const QString &sql,const QVariantList &args={}){
 QSqlQuery q(db);q.prepare(sql);for(const auto &a:args)q.addBindValue(a);q.exec();return q;
}
static bool initDb(){
 const QString folder=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
 QDir().mkpath(folder);
 db=QSqlDatabase::addDatabase("QSQLITE");db.setDatabaseName(folder+"/kasapro.sqlite");
 if(!db.open())return false;
 run("PRAGMA foreign_keys=ON");run("PRAGMA busy_timeout=5000");run("PRAGMA journal_mode=WAL");
 const QStringList ddl={
 "CREATE TABLE IF NOT EXISTS parties(id INTEGER PRIMARY KEY,name TEXT NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('Musteri','Tedarikci','Her ikisi')),phone TEXT DEFAULT '',tax_no TEXT DEFAULT '',balance_cents INTEGER NOT NULL DEFAULT 0)",
 "CREATE TABLE IF NOT EXISTS employees(id INTEGER PRIMARY KEY,name TEXT NOT NULL,role TEXT NOT NULL DEFAULT 'Personel',salary_cents INTEGER NOT NULL DEFAULT 0,active INTEGER NOT NULL DEFAULT 1)",
 "CREATE TABLE IF NOT EXISTS warehouses(id INTEGER PRIMARY KEY,name TEXT NOT NULL UNIQUE)",
 "CREATE TABLE IF NOT EXISTS products(id INTEGER PRIMARY KEY,sku TEXT NOT NULL UNIQUE,name TEXT NOT NULL,price_cents INTEGER NOT NULL DEFAULT 0,cost_cents INTEGER NOT NULL DEFAULT 0,minimum_qty INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",
 "CREATE TABLE IF NOT EXISTS stock(product_id INTEGER NOT NULL,warehouse_id INTEGER NOT NULL,qty INTEGER NOT NULL DEFAULT 0,reserved INTEGER NOT NULL DEFAULT 0,CHECK(qty>=0 AND reserved>=0 AND reserved<=qty),PRIMARY KEY(product_id,warehouse_id),FOREIGN KEY(product_id) REFERENCES products(id),FOREIGN KEY(warehouse_id) REFERENCES warehouses(id))",
 "CREATE TABLE IF NOT EXISTS docs(id INTEGER PRIMARY KEY,doc_type TEXT NOT NULL,status TEXT NOT NULL,party_id INTEGER,employee_id INTEGER,total_cents INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,FOREIGN KEY(party_id) REFERENCES parties(id))",
 "CREATE TABLE IF NOT EXISTS doc_lines(id INTEGER PRIMARY KEY,doc_id INTEGER NOT NULL,product_id INTEGER NOT NULL,warehouse_id INTEGER NOT NULL,qty INTEGER NOT NULL,unit_cents INTEGER NOT NULL,FOREIGN KEY(doc_id) REFERENCES docs(id),FOREIGN KEY(product_id) REFERENCES products(id))",
 "CREATE TABLE IF NOT EXISTS stock_moves(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL,warehouse_id INTEGER NOT NULL,delta_qty INTEGER NOT NULL,reason TEXT NOT NULL,doc_id INTEGER,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",
 "CREATE TABLE IF NOT EXISTS cash_moves(id INTEGER PRIMARY KEY,kind TEXT NOT NULL,amount_cents INTEGER NOT NULL,description TEXT NOT NULL,doc_id INTEGER,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",
 "CREATE TABLE IF NOT EXISTS reservations(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL,warehouse_id INTEGER NOT NULL,qty INTEGER NOT NULL,customer_id INTEGER,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,expires_at TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'Aktif')",
 "CREATE TABLE IF NOT EXISTS held_carts(id INTEGER PRIMARY KEY,label TEXT NOT NULL,cart_json TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",
 "CREATE INDEX IF NOT EXISTS ix_docs_created ON docs(created_at)",
 "CREATE INDEX IF NOT EXISTS ix_moves_created ON stock_moves(created_at)"};
 for(const auto &s:ddl)if(!run(s))return false;
 run("INSERT OR IGNORE INTO warehouses(id,name) VALUES(1,'Ana Depo')");
 // Schema migration: previous installations retain their products and movements.
 auto cols=query("PRAGMA table_info(products)");bool hasImage=false;
 while(cols.next())if(cols.value(1).toString()=="image_path")hasImage=true;
 if(!hasImage && !run("ALTER TABLE products ADD COLUMN image_path TEXT NOT NULL DEFAULT ''"))return false;
 // V1.5: separate EAN/UPC/Code128 barcode, preserving existing SKU and product rows.
 auto barcodeColumns=query("PRAGMA table_info(products)");bool hasBarcode=false;
 while(barcodeColumns.next())if(barcodeColumns.value(1).toString()=="barcode")hasBarcode=true;
 if(!hasBarcode && !run("ALTER TABLE products ADD COLUMN barcode TEXT NOT NULL DEFAULT ''"))return false;
 if(!run("CREATE UNIQUE INDEX IF NOT EXISTS ix_products_barcode ON products(barcode) WHERE barcode<>''"))return false;

  if(!run("CREATE TABLE IF NOT EXISTS party_movements(id INTEGER PRIMARY KEY,party_id INTEGER NOT NULL REFERENCES parties(id),kind TEXT NOT NULL,amount_cents INTEGER NOT NULL,description TEXT NOT NULL DEFAULT '',created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))return false;
  if(!run("CREATE TABLE IF NOT EXISTS stock_counts(id INTEGER PRIMARY KEY,warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),product_id INTEGER NOT NULL REFERENCES products(id),system_qty INTEGER NOT NULL,counted_qty INTEGER NOT NULL,delta_qty INTEGER NOT NULL,reason TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))return false;
  if(!run("CREATE INDEX IF NOT EXISTS ix_stock_counts_date ON stock_counts(created_at)"))return false;
  // Additive migrations: older kasapro.sqlite databases remain intact.
  if(!run("CREATE TABLE IF NOT EXISTS pos_payments(id INTEGER PRIMARY KEY,doc_id INTEGER NOT NULL REFERENCES docs(id),method TEXT NOT NULL CHECK(method IN ('Nakit','Kart','Veresiye')),amount_cents INTEGER NOT NULL CHECK(amount_cents>0),created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))return false;
  if(!run("CREATE INDEX IF NOT EXISTS ix_pos_payments_doc ON pos_payments(doc_id)"))return false;
  if(!run("CREATE TABLE IF NOT EXISTS cash_shifts(id INTEGER PRIMARY KEY,opened_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,closed_at TEXT,operator_name TEXT NOT NULL,opening_cents INTEGER NOT NULL,expected_cents INTEGER, counted_cents INTEGER, difference_cents INTEGER, opening_move_id INTEGER NOT NULL DEFAULT 0,status TEXT NOT NULL DEFAULT 'Acik' CHECK(status IN ('Acik','Kapali')))"))return false;
  if(!run("CREATE UNIQUE INDEX IF NOT EXISTS ix_one_open_cash_shift ON cash_shifts(status) WHERE status='Acik'"))return false;
  { QString paymentError; if(!PaymentAttemptStore::migrate(db,&paymentError))return false; }
  if(!run("CREATE TABLE IF NOT EXISTS purchase_orders(id INTEGER PRIMARY KEY,supplier_id INTEGER NOT NULL REFERENCES parties(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),status TEXT NOT NULL DEFAULT 'Bekliyor' CHECK(status IN ('Bekliyor','Kabul Edildi')),note TEXT NOT NULL DEFAULT '',total_cents INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,received_at TEXT)"))return false;
  if(!run("CREATE TABLE IF NOT EXISTS purchase_order_lines(id INTEGER PRIMARY KEY,order_id INTEGER NOT NULL REFERENCES purchase_orders(id),product_id INTEGER NOT NULL REFERENCES products(id),qty INTEGER NOT NULL CHECK(qty>0),cost_cents INTEGER NOT NULL CHECK(cost_cents>=0))"))return false;
  if(!run("CREATE TABLE IF NOT EXISTS supplier_payables(id INTEGER PRIMARY KEY, supplier_id INTEGER NOT NULL REFERENCES parties(id), purchase_order_id INTEGER NOT NULL UNIQUE REFERENCES purchase_orders(id), doc_id INTEGER NOT NULL REFERENCES docs(id), original_cents INTEGER NOT NULL CHECK(original_cents>0), paid_cents INTEGER NOT NULL DEFAULT 0 CHECK(paid_cents>=0 AND paid_cents<=original_cents), due_date TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))return false;
  if(!run("CREATE INDEX IF NOT EXISTS ix_supplier_payables_supplier ON supplier_payables(supplier_id,due_date)"))return false;
  if(!run("CREATE TABLE IF NOT EXISTS supplier_payable_payments(id INTEGER PRIMARY KEY, payable_id INTEGER NOT NULL REFERENCES supplier_payables(id), supplier_id INTEGER NOT NULL REFERENCES parties(id), amount_cents INTEGER NOT NULL CHECK(amount_cents>0), cash_move_id INTEGER NOT NULL REFERENCES cash_moves(id), note TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))return false;
  if(!run("CREATE TABLE IF NOT EXISTS app_seed_history(key TEXT PRIMARY KEY,installed_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))return false;
  if(!run("CREATE TABLE IF NOT EXISTS product_barcodes(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL REFERENCES products(id) ON DELETE CASCADE,barcode TEXT NOT NULL UNIQUE,description TEXT NOT NULL DEFAULT '',created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))return false;
  if(!run("CREATE INDEX IF NOT EXISTS ix_product_barcodes_product ON product_barcodes(product_id)"))return false;
  // V1.7: add a pack multiplier to alternative barcodes without altering old rows.
  QSqlQuery packColumns=query("PRAGMA table_info(product_barcodes)");bool hasMultiplier=false;
  while(packColumns.next())if(packColumns.value(1).toString()=="unit_multiplier")hasMultiplier=true;
  if(!hasMultiplier && !run("ALTER TABLE product_barcodes ADD COLUMN unit_multiplier INTEGER NOT NULL DEFAULT 1 CHECK(unit_multiplier BETWEEN 1 AND 10000)"))return false;
 // V3.0: generic catalog metadata, independent of a particular retail sector.
 if(!run("CREATE TABLE IF NOT EXISTS app_profile(id INTEGER PRIMARY KEY CHECK(id=1),sector TEXT NOT NULL DEFAULT 'Genel Perakende',updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))return false;
 if(!run("INSERT OR IGNORE INTO app_profile(id,sector) VALUES(1,'Genel Perakende')"))return false;
 if(!run("CREATE TABLE IF NOT EXISTS product_details(product_id INTEGER PRIMARY KEY REFERENCES products(id) ON DELETE CASCADE,category TEXT NOT NULL DEFAULT '',brand TEXT NOT NULL DEFAULT '',base_unit TEXT NOT NULL DEFAULT 'Adet',variant_axis1 TEXT NOT NULL DEFAULT '',variant_value1 TEXT NOT NULL DEFAULT '',variant_axis2 TEXT NOT NULL DEFAULT '',variant_value2 TEXT NOT NULL DEFAULT '',description TEXT NOT NULL DEFAULT '')"))return false;
 if(!run("CREATE TABLE IF NOT EXISTS product_variants(parent_product_id INTEGER NOT NULL REFERENCES products(id),child_product_id INTEGER NOT NULL UNIQUE REFERENCES products(id),option1 TEXT NOT NULL,option2 TEXT NOT NULL DEFAULT '',created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,PRIMARY KEY(parent_product_id,option1,option2),CHECK(parent_product_id<>child_product_id))"))return false;
 if(!run("CREATE INDEX IF NOT EXISTS ix_variant_parent ON product_variants(parent_product_id)"))return false;
 QString packagingError;if(!PackagingCenter::install(db,&packagingError)){qWarning()<<packagingError;return false;}
 QString cartonError;if(!CartonOpeningCenter::install(db,&cartonError)){qWarning()<<cartonError;return false;}
 QString cartonSaleError;if(!CartonSaleEngine::install(db,&cartonSaleError)){qWarning()<<cartonSaleError;return false;}
 QString fractionalError;if(!FractionalStock::install(db,&fractionalError)){qWarning()<<fractionalError;return false;}
 if(!FractionalTrade::install(db,&fractionalError)){qWarning()<<fractionalError;return false;}
 return true;
}
// Demo verisi yalnızca istek üzerine içe aktarılır; mevcut ürün ve stoklar değiştirilmez.
struct DemoProduct {const char *sku; QString name; int price; int cost; int minQty;};
static bool installMarketDemo(QString *failure=nullptr){
  QSqlQuery installed=query("SELECT key FROM app_seed_history WHERE key='market-tr-v12'");
  if(installed.next()){if(failure)*failure="Demo ürünleri daha önce yüklendi.";return false;}
  const QVector<DemoProduct> items={
        {"ICE-001",QString::fromUtf8("Coca Cola 1 L"),4500,3300,10},
        {"ICE-002",QString::fromUtf8("Coca Cola 2.5 L"),7200,5600,8},
        {"ICE-003",QString::fromUtf8("Fanta 1 L"),4200,3100,8},
        {"ICE-004",QString::fromUtf8("Sprite 1 L"),4200,3100,8},
        {"ICE-005",QString::fromUtf8("Ayran 1 L"),3600,2500,10},
        {"ICE-006",QString::fromUtf8("Şalgam 1 L"),3900,2800,8},
        {"ICE-007",QString::fromUtf8("Meyve Suyu Portakal 1 L"),5200,3900,6},
        {"ICE-008",QString::fromUtf8("Maden Suyu 6x200 ml"),3800,2700,10},
        {"ICE-009",QString::fromUtf8("Su 1.5 L"),1200,700,24},
        {"ICE-010",QString::fromUtf8("Enerji İçeceği 250 ml"),3500,2400,12},
        {"SUT-001",QString::fromUtf8("Süt 1 L"),3250,2450,10},
        {"SUT-002",QString::fromUtf8("Yoğurt 1 Kg"),5900,4700,8},
        {"SUT-003",QString::fromUtf8("Kaşar Peyniri 600 g"),14500,11800,5},
        {"SUT-004",QString::fromUtf8("Beyaz Peynir 800 g"),12900,10300,5},
        {"SUT-005",QString::fromUtf8("Tereyağı 1 Kg"),21500,18700,4},
        {"SUT-006",QString::fromUtf8("Labne 180 g"),4200,3100,8},
        {"SUT-007",QString::fromUtf8("Krema 200 ml"),3600,2500,8},
        {"SUT-008",QString::fromUtf8("Yumurta 15li"),8500,6900,6},
        {"UNM-001",QString::fromUtf8("Ekmek 250 g"),1250,850,25},
        {"UNM-002",QString::fromUtf8("Tam Buğday Ekmek"),1850,1300,12},
        {"UNM-003",QString::fromUtf8("Simit"),1500,1000,20},
        {"UNM-004",QString::fromUtf8("Poğaça Peynirli"),2250,1600,15},
        {"UNM-005",QString::fromUtf8("Tost Ekmeği"),3900,2900,10},
        {"UNM-006",QString::fromUtf8("Hamburger Ekmeği 4lü"),4400,3300,8},
        {"KRT-001",QString::fromUtf8("Makarna Burgu 500 g"),2400,1700,12},
        {"KRT-002",QString::fromUtf8("Pirinç 2.5 Kg"),9800,8100,6},
        {"KRT-003",QString::fromUtf8("Bulgur Pilavlık 1 Kg"),4200,3100,8},
        {"KRT-004",QString::fromUtf8("Un 5 Kg"),12300,10100,5},
        {"KRT-005",QString::fromUtf8("Toz Şeker 5 Kg"),17600,14900,4},
        {"KRT-006",QString::fromUtf8("Tuz 750 g"),950,550,15},
        {"KRT-007",QString::fromUtf8("Ayçiçek Yağı 5 L"),28900,25500,4},
        {"KRT-008",QString::fromUtf8("Zeytinyağı 1 L"),21500,18700,4},
        {"KRT-009",QString::fromUtf8("Domates Salçası 830 g"),6900,5400,6},
        {"KRT-010",QString::fromUtf8("Ketçap 600 g"),5300,4100,6},
        {"KRT-011",QString::fromUtf8("Mayonez 560 g"),5700,4500,6},
        {"KRT-012",QString::fromUtf8("Reçel Çilek 380 g"),4900,3700,8},
        {"KRT-013",QString::fromUtf8("Bal 850 g"),18900,16100,4},
        {"ATI-001",QString::fromUtf8("Ton Balığı 2x160 g"),9500,7800,6},
        {"ATI-002",QString::fromUtf8("Nohut 1 Kg"),5600,4300,8},
        {"ATI-003",QString::fromUtf8("Kuru Fasulye 1 Kg"),6900,5400,8},
        {"ATI-004",QString::fromUtf8("Mercimek Kırmızı 1 Kg"),6100,4700,8},
        {"ATI-005",QString::fromUtf8("Hazır Çorba Tavuk"),2200,1500,10},
        {"ATI-006",QString::fromUtf8("Hazır Çorba Mercimek"),2200,1500,10},
        {"ATI-007",QString::fromUtf8("Zeytin Siyah 800 g"),9800,7900,6},
        {"ATI-008",QString::fromUtf8("Turşu Karışık 680 g"),4500,3300,6},
        {"ATI-009",QString::fromUtf8("Patates Cipsi 165 g"),4900,3600,10},
        {"ATI-010",QString::fromUtf8("Kraker Tuzlu"),2600,1800,10},
        {"ATI-011",QString::fromUtf8("Bisküvi Kremalı"),3100,2200,12},
        {"ATI-012",QString::fromUtf8("Çikolata Sütlü 80 g"),2900,2000,12},
        {"ATI-013",QString::fromUtf8("Gofret Fındıklı"),1800,1200,16},
        {"ATI-014",QString::fromUtf8("Kahve Türk 100 g"),6400,5100,8},
        {"ATI-015",QString::fromUtf8("Çay 1 Kg"),15900,13200,5},
        {"ATI-016",QString::fromUtf8("Hazır Kahve 100 g"),7900,6400,6},
        {"MEY-001",QString::fromUtf8("Elma Starking 1 Kg"),4200,2800,10},
        {"MEY-002",QString::fromUtf8("Muz Yerli 1 Kg"),5900,4300,10},
        {"MEY-003",QString::fromUtf8("Portakal 1 Kg"),3800,2400,10},
        {"MEY-004",QString::fromUtf8("Domates 1 Kg"),3500,2200,12},
        {"MEY-005",QString::fromUtf8("Salatalık 1 Kg"),3300,2100,10},
        {"MEY-006",QString::fromUtf8("Patates 2 Kg"),6500,4700,8},
        {"MEY-007",QString::fromUtf8("Soğan 2 Kg"),5900,4300,8},
        {"MEY-008",QString::fromUtf8("Biber Çarliston 500 g"),4200,3000,8},
        {"MEY-009",QString::fromUtf8("Marul Adet"),2500,1600,12},
        {"MEY-010",QString::fromUtf8("Maydanoz Demet"),900,500,20},
        {"TEM-001",QString::fromUtf8("Bulaşık Deterjanı 1.5 L"),9500,7700,6},
        {"TEM-002",QString::fromUtf8("Çamaşır Suyu 750 ml"),3900,2800,8},
        {"TEM-003",QString::fromUtf8("Yumuşatıcı 3 L"),14900,12200,4},
        {"TEM-004",QString::fromUtf8("Çamaşır Deterjanı 6 Kg"),28900,25100,4},
        {"TEM-005",QString::fromUtf8("Sıvı Sabun 500 ml"),3600,2500,10},
        {"TEM-006",QString::fromUtf8("Kağıt Havlu 6lı"),11900,9600,5},
        {"TEM-007",QString::fromUtf8("Tuvalet Kağıdı 16lı"),18900,15700,4},
        {"TEM-008",QString::fromUtf8("Islak Mendil 90lı"),2600,1700,10},
        {"TEM-009",QString::fromUtf8("Sünger 10lu"),2400,1500,10},
        {"TEM-010",QString::fromUtf8("Çöp Torbası Büyük Boy"),3200,2200,10},
        {"KIS-001",QString::fromUtf8("Şampuan 600 ml"),9800,7900,6},
        {"KIS-002",QString::fromUtf8("Diş Macunu 100 ml"),4200,3100,10},
        {"KIS-003",QString::fromUtf8("Diş Fırçası Orta"),2900,1900,12},
        {"KIS-004",QString::fromUtf8("Duş Jeli 500 ml"),6900,5400,8},
        {"KIS-005",QString::fromUtf8("Deodorant 150 ml"),8900,7100,8},
        {"KIS-006",QString::fromUtf8("Bebek Bezi 4 Numara 40lı"),24900,21900,4},
        {"KIS-007",QString::fromUtf8("Bebek Şampuanı"),5900,4600,6},
        {"KIS-008",QString::fromUtf8("Kağıt Mendil 10lu"),6900,5400,8},
        {"EV-001",QString::fromUtf8("Alüminyum Folyo"),3400,2400,8},
        {"EV-002",QString::fromUtf8("Streç Film"),3200,2200,8},
        {"EV-003",QString::fromUtf8("Buzdolabı Poşeti"),2400,1600,10},
        {"EV-004",QString::fromUtf8("Pil AA 4lü"),5600,4300,8},
        {"EV-005",QString::fromUtf8("Ampul Led 12W"),7800,6200,6},
        {"ET-001",QString::fromUtf8("Tavuk Göğüs Kg"),16900,14800,4},
        {"ET-002",QString::fromUtf8("Dana Kıyma Kg"),34900,31700,3},
        {"ET-003",QString::fromUtf8("Sucuk 350 g"),12900,10400,5},
        {"ET-004",QString::fromUtf8("Sosis 500 g"),7800,6200,6},
        {"ET-005",QString::fromUtf8("Salam 250 g"),5900,4500,6},
        {"DON-001",QString::fromUtf8("Dondurma Kakaolu 1 L"),9200,7400,5},
        {"DON-002",QString::fromUtf8("Pizza Dondurulmuş"),11900,9700,5},
        {"DON-003",QString::fromUtf8("Patates Kroket 1 Kg"),10500,8600,5},
  };
  if(!db.transaction()){if(failure)*failure=db.lastError().text();return false;}
  bool ok=true;int inserted=0;
  for(const auto &p:items){
    QSqlQuery exists=query("SELECT id FROM products WHERE sku=?",{QString::fromLatin1(p.sku)});
    if(exists.next())continue;
    if(!run("INSERT INTO products(sku,name,price_cents,cost_cents,minimum_qty,image_path) VALUES(?,?,?,?,?,'')",{QString::fromLatin1(p.sku),p.name,p.price,p.cost,p.minQty})){ok=false;break;}
    QSqlQuery idQuery=query("SELECT last_insert_rowid()");if(!idQuery.next()){ok=false;break;}
    const int id=idQuery.value(0).toInt();
    // Başlangıç test stoğu yalnızca yeni eklenen demo ürünlere yazılır.
    ok=run("INSERT INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,1,30,0)",{id})
      && run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason) VALUES(?,1,30,'Market demo verisi')",{id});
    if(!ok)break;++inserted;
  }
  if(ok)ok=run("INSERT INTO app_seed_history(key) VALUES('market-tr-v12')");
  if(ok&&db.commit())return true;
  db.rollback();if(failure)*failure="Demo yükleme geri alındı: "+db.lastError().text();return false;
}
static QString marketCategory(const QString &sku){
  const QString prefix=sku.section('-',0,0);
  const QMap<QString,QString> names={{"ICE","İçecekler"},{"SUT","Süt & Kahvaltı"},{"UNM","Unlu Mamuller"},{"KRT","Temel Gıda"},{"ATI","Atıştırmalık"},{"MEY","Meyve & Sebze"},{"TEM","Temizlik"},{"KIS","Kişisel Bakım"},{"EV","Ev Ürünleri"},{"ET","Et & Şarküteri"},{"DON","Dondurulmuş"}};
  return names.value(prefix,prefix);
}
static QString money(qint64 cents){return QLocale(QLocale::Turkish,QLocale::Turkey).toString(cents/100.0,'f',2)+" TL";}
static qint64 toCents(double n){return qRound64(n*100.0);}
static QString err(){return db.lastError().text();}

class BackofficeWindow:public QMainWindow{
 QTabWidget *tabs; QTableWidget *table; QComboBox *viewSelect; QComboBox *product,*warehouse,*customer,*payment;QSpinBox *quantity;QLabel *totalLabel,*dashboardLabel;QTableWidget *cartTable; QLabel *productPreview=nullptr,*availabilityLabel=nullptr,*warehouseSummary=nullptr;QComboBox *warehouseFilter=nullptr;QTableWidget *warehouseTable=nullptr;QLineEdit *warehouseSearch=nullptr;QVector<QPair<int,int>> cart;int heldId=0;
 QMap<QString,QString> views={
 {"Cari Kartlar","SELECT id AS ID,name AS Ad,kind AS Tur,phone AS Telefon,tax_no AS Vergi_No,balance_cents/100.0 AS Bakiye_TL FROM parties ORDER BY id DESC"},
 {"Personel","SELECT id AS ID,name AS Ad,role AS Gorev,salary_cents/100.0 AS Maas_TL,active AS Aktif FROM employees ORDER BY id DESC"},
 {"Depolar","SELECT * FROM warehouses ORDER BY id"},
 {"Urunler","SELECT id AS ID,sku AS Stok_Kodu,name AS Urun,price_cents/100.0 AS Satis_TL,cost_cents/100.0 AS Maliyet_TL,minimum_qty AS Kritik_Stok FROM products ORDER BY id DESC"},
 {"Stok Durumu","SELECT p.sku AS Stok_Kodu,p.name AS Urun,w.name AS Depo,s.qty AS Fiziki,s.reserved AS Rezerve,(s.qty-s.reserved) AS Kullanilabilir FROM stock s JOIN products p ON p.id=s.product_id JOIN warehouses w ON w.id=s.warehouse_id ORDER BY p.name"},
 {"Stok Hareketleri","SELECT m.id AS ID,p.name AS Urun,w.name AS Depo,m.delta_qty AS Miktar,m.reason AS Aciklama,m.created_at AS Tarih FROM stock_moves m JOIN products p ON p.id=m.product_id JOIN warehouses w ON w.id=m.warehouse_id ORDER BY m.id DESC"},
 {"Belgeler","SELECT d.id AS ID,d.doc_type AS Tur,d.status AS Durum,COALESCE(p.name,'-') AS Cari,d.total_cents/100.0 AS Tutar_TL,d.created_at AS Tarih FROM docs d LEFT JOIN parties p ON p.id=d.party_id ORDER BY d.id DESC"},
 {"Rezervasyonlar","SELECT r.id AS ID,p.name AS Urun,w.name AS Depo,r.qty AS Miktar,r.status AS Durum,r.expires_at AS Bitis FROM reservations r JOIN products p ON p.id=r.product_id JOIN warehouses w ON w.id=r.warehouse_id ORDER BY r.id DESC"},
 {"Kasa Hareketleri","SELECT id AS ID,kind AS Tur,amount_cents/100.0 AS Tutar_TL,description AS Aciklama,created_at AS Tarih FROM cash_moves ORDER BY id DESC"},
 {"Satis Istatistigi","SELECT p.sku AS Stok_Kodu,p.name AS Urun,SUM(l.qty) AS Adet,SUM(l.qty*l.unit_cents)/100.0 AS Ciro_TL FROM doc_lines l JOIN docs d ON d.id=l.doc_id JOIN products p ON p.id=l.product_id WHERE d.doc_type='Satis' AND d.status='Tamamlandi' GROUP BY p.id ORDER BY Ciro_TL DESC"},
 {"Stok Yaslandirma","SELECT p.sku AS Stok_Kodu,p.name AS Urun,SUM(s.qty) AS Stok,CAST(julianday('now')-julianday(p.created_at) AS INTEGER) AS Kayittan_Beri_Gun,MAX(COALESCE((SELECT MAX(m.created_at) FROM stock_moves m WHERE m.product_id=p.id),'Yok')) AS Son_Hareket FROM products p JOIN stock s ON p.id=s.product_id GROUP BY p.id ORDER BY Kayittan_Beri_Gun DESC"}
 };
 void message(const QString &m){QMessageBox::information(this,"KasaPro",m);}
 void failure(const QString &m){QMessageBox::warning(this,"Islem basarisiz",m);}
 static void fill(QTableWidget *t,QSqlQuery q){t->clear();QSqlRecord rec=q.record();t->setColumnCount(rec.count());QStringList headers;for(int i=0;i<rec.count();i++)headers<<rec.fieldName(i);t->setHorizontalHeaderLabels(headers);t->setRowCount(0);while(q.next()){int r=t->rowCount();t->insertRow(r);for(int c=0;c<rec.count();c++){auto *item=new QTableWidgetItem(q.value(c).toString());t->setItem(r,c,item);}}t->resizeColumnsToContents();}
 void refreshCombo(QComboBox *cb,const QString &sql){int old=cb->currentData().toInt();cb->clear();auto q=query(sql);while(q.next())cb->addItem(q.value(1).toString(),q.value(0));int i=cb->findData(old);if(i>=0)cb->setCurrentIndex(i);}
 void refresh(){refreshCombo(product,"SELECT id,sku||' - '||name FROM products ORDER BY name");refreshCombo(warehouse,"SELECT id,name FROM warehouses ORDER BY name");refreshCombo(customer,"SELECT id,name FROM parties ORDER BY name");if(table)showView();refreshCart();dashboard();refreshWarehouseExplorer();refreshProductPreview();}
 void showView(){auto q=query(views.value(viewSelect->currentText()));fill(table,q);if(viewSelect->currentText()=="Urunler"){
  // Pictures in product rows, not only a filesystem path.
  table->insertColumn(0);table->setHorizontalHeaderItem(0,new QTableWidgetItem("Gorsel"));
  for(int r=0;r<table->rowCount();++r){int id=table->item(r,1)->text().toInt();auto im=query("SELECT image_path FROM products WHERE id=?",{id});
   auto *label=new QLabel;label->setAlignment(Qt::AlignCenter);if(im.next()){QPixmap pix(im.value(0).toString());if(!pix.isNull())label->setPixmap(pix.scaled(50,50,Qt::KeepAspectRatio,Qt::SmoothTransformation));}
   table->setCellWidget(r,0,label);table->setRowHeight(r,60);
  }table->setColumnWidth(0,80);
 }}
 void refreshProductPreview(){if(!productPreview||!availabilityLabel||!product||!warehouse)return;
  auto q=query("SELECT image_path FROM products WHERE id=?",{product->currentData()});QPixmap px;
  if(q.next())px.load(q.value(0).toString());productPreview->setPixmap(px.isNull()?QPixmap():px.scaled(154,142,Qt::KeepAspectRatio,Qt::SmoothTransformation));
  if(px.isNull())productPreview->setText("URUN GORSELI YOK");
  auto st=query("SELECT qty,reserved FROM stock WHERE product_id=? AND warehouse_id=?",{product->currentData(),warehouse->currentData()});
  int phy=0,res=0;if(st.next()){phy=st.value(0).toInt();res=st.value(1).toInt();}
  availabilityLabel->setText(QString("%1 • Fiziki %2   |   Rezerve %3   |   Kullanilabilir %4").arg(warehouse->currentText()).arg(phy).arg(res).arg(phy-res));
 }
 void refreshWarehouseExplorer(){if(!warehouseFilter||!warehouseTable)return;
  int chosen=warehouseFilter->currentData().toInt();warehouseFilter->blockSignals(true);
  refreshCombo(warehouseFilter,"SELECT id,name FROM warehouses ORDER BY name");
  int idx=warehouseFilter->findData(chosen);if(idx>=0)warehouseFilter->setCurrentIndex(idx);warehouseFilter->blockSignals(false);
  QString search=warehouseSearch?warehouseSearch->text().trimmed():QString();
  auto q=query("SELECT p.id,p.sku,p.name,COALESCE(s.qty,0) AS fiziksel,COALESCE(s.reserved,0) AS rezerve,COALESCE(s.qty-s.reserved,0) AS kullanilabilir,p.minimum_qty,p.image_path FROM products p LEFT JOIN stock s ON s.product_id=p.id AND s.warehouse_id=? WHERE (p.sku LIKE ? OR p.name LIKE ?) ORDER BY p.name",{warehouseFilter->currentData(),"%"+search+"%","%"+search+"%"});
  warehouseTable->clear();warehouseTable->setColumnCount(7);warehouseTable->setHorizontalHeaderLabels({"Gorsel","Stok kodu","Urun","Fiziki","Rezerve","Kullanilabilir","Kritik stok"});warehouseTable->setRowCount(0);
  int count=0,physical=0,reserved=0,available=0;
  while(q.next()){int r=warehouseTable->rowCount();warehouseTable->insertRow(r);auto *photo=new QLabel;photo->setAlignment(Qt::AlignCenter);QPixmap pm(q.value(7).toString());if(!pm.isNull())photo->setPixmap(pm.scaled(54,54,Qt::KeepAspectRatio,Qt::SmoothTransformation));warehouseTable->setCellWidget(r,0,photo);
   for(int c=1;c<7;++c){int src=c==1?1:c==2?2:c==3?3:c==4?4:c==5?5:6;auto *it=new QTableWidgetItem(q.value(src).toString());if(c>=3)it->setTextAlignment(Qt::AlignCenter);warehouseTable->setItem(r,c,it);}
   warehouseTable->setRowHeight(r,68);int qty=q.value(3).toInt(),res=q.value(4).toInt();if(qty-res<=q.value(6).toInt())for(int c=3;c<=5;++c)warehouseTable->item(r,c)->setForeground(QColor("#d97706"));count++;physical+=qty;reserved+=res;available+=qty-res;
  }warehouseTable->setColumnWidth(0,82);warehouseTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
  if(warehouseSummary)warehouseSummary->setText(QString("URUN CESIDI  %1      FIZIKI  %2      REZERVE  %3      KULLANILABILIR  %4").arg(count).arg(physical).arg(reserved).arg(available));
 }
 void dashboard(){auto q=query("SELECT (SELECT COUNT(*) FROM products),(SELECT COUNT(*) FROM parties),(SELECT COALESCE(SUM(qty-reserved),0) FROM stock),(SELECT COALESCE(SUM(amount_cents),0) FROM cash_moves),(SELECT COALESCE(SUM(total_cents),0) FROM docs WHERE doc_type='Satis' AND status='Tamamlandi')");if(q.next())dashboardLabel->setText(QString("Urun: %1      Cari: %2      Kullanilabilir stok: %3      Kasa: %4      Toplam satis: %5").arg(q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),money(q.value(3).toLongLong()),money(q.value(4).toLongLong())));}
 QFormLayout* form(QDialog &d,const QString &title){d.setWindowTitle(title);d.resize(460,300);auto *layout=new QVBoxLayout(&d);auto *f=new QFormLayout;layout->addLayout(f);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);return f;}
 QDoubleSpinBox* amount(double max=100000000){auto *v=new QDoubleSpinBox;v->setRange(0,max);v->setDecimals(2);v->setSuffix(" TL");return v;}
 void addParty(){QDialog d(this);auto *f=form(d,"Yeni Cari Kart");auto *name=new QLineEdit,*phone=new QLineEdit,*tax=new QLineEdit;auto *kind=new QComboBox;kind->addItems({"Musteri","Tedarikci","Her ikisi"});f->addRow("Unvan / ad",name);f->addRow("Tur",kind);f->addRow("Telefon",phone);f->addRow("Vergi / TC no",tax);if(d.exec()!=QDialog::Accepted)return;if(name->text().trimmed().isEmpty())return failure("Ad gereklidir.");if(!run("INSERT INTO parties(name,kind,phone,tax_no) VALUES(?,?,?,?)",{name->text().trimmed(),kind->currentText(),phone->text(),tax->text()}))return failure("Cari kaydedilemedi.");refresh();}
 void addEmployee(){QDialog d(this);auto *f=form(d,"Yeni Personel");auto *name=new QLineEdit,*role=new QLineEdit;auto *salary=amount();f->addRow("Ad soyad",name);f->addRow("Gorev",role);f->addRow("Aylik ucret",salary);if(d.exec()!=QDialog::Accepted)return;if(name->text().trimmed().isEmpty())return failure("Ad gereklidir.");run("INSERT INTO employees(name,role,salary_cents) VALUES(?,?,?)",{name->text(),role->text(),toCents(salary->value())});refresh();}
 void addWarehouse(){bool ok=false;auto name=QInputDialog::getText(this,"Yeni depo","Depo adi",QLineEdit::Normal,"",&ok);if(ok && !name.trimmed().isEmpty()){if(!run("INSERT INTO warehouses(name) VALUES(?)",{name.trimmed()}))failure("Depo adi zaten kullaniliyor olabilir.");refresh();}}
 void addProduct(){QDialog d(this);auto *f=form(d,"Yeni Urun");auto *sku=new QLineEdit,*name=new QLineEdit;auto *price=amount(),*cost=amount();auto *min=new QSpinBox;min->setRange(0,1000000);auto *imagePath=new QLineEdit;imagePath->setReadOnly(true);auto *imageBtn=new QPushButton("Fotograf sec...");auto *photoRow=new QWidget;auto *photoLay=new QHBoxLayout(photoRow);photoLay->setContentsMargins(0,0,0,0);photoLay->addWidget(imagePath);photoLay->addWidget(imageBtn);QObject::connect(imageBtn,&QPushButton::clicked,&d,[&]{QString path=QFileDialog::getOpenFileName(&d,"Urun gorseli sec",QString(),"Gorseller (*.png *.jpg *.jpeg *.webp *.bmp)");if(!path.isEmpty())imagePath->setText(path);});f->addRow("Stok kodu",sku);f->addRow("Urun",name);f->addRow("Satis fiyati",price);f->addRow("Alis maliyeti",cost);f->addRow("Kritik stok",min);f->addRow("Urun fotografi",photoRow);if(d.exec()!=QDialog::Accepted)return;if(sku->text().trimmed().isEmpty() || name->text().trimmed().isEmpty())return failure("Kod ve ad zorunlu.");QString storedImage;
 if(!imagePath->text().isEmpty()){QString folder=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/product_images";QDir().mkpath(folder);storedImage=folder+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+"."+QFileInfo(imagePath->text()).suffix().toLower();if(!QFile::copy(imagePath->text(),storedImage))return failure("Fotograf uygulama veri dizinine kopyalanamadi.");}
 if(!run("INSERT INTO products(sku,name,price_cents,cost_cents,minimum_qty,image_path) VALUES(?,?,?,?,?,?)",{sku->text(),name->text(),toCents(price->value()),toCents(cost->value()),min->value(),storedImage}))return failure("Stok kodu benzersiz olmalidir.");refresh();}
 void stockEntry(){QDialog d(this);auto *f=form(d,"Stok Giris / Duzeltme");auto *p=new QComboBox;auto *w=new QComboBox;auto *qty=new QSpinBox;qty->setRange(-1000000,1000000);qty->setValue(1);auto *reason=new QLineEdit("Stok duzeltme");auto qq=query("SELECT id,sku||' - '||name FROM products");while(qq.next())p->addItem(qq.value(1).toString(),qq.value(0));qq=query("SELECT id,name FROM warehouses");while(qq.next())w->addItem(qq.value(1).toString(),qq.value(0));f->addRow("Urun",p);f->addRow("Depo",w);f->addRow("Degisim (+/-)",qty);f->addRow("Aciklama",reason);if(d.exec()!=QDialog::Accepted)return;if(p->currentIndex()<0 || w->currentIndex()<0 || qty->value()==0)return failure("Urun, depo ve miktar giriniz.");int pid=p->currentData().toInt(),wid=w->currentData().toInt(),n=qty->value();if(!db.transaction())return failure(err());bool ok=run("INSERT OR IGNORE INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,?,0,0)",{pid,wid}) && run("UPDATE stock SET qty=qty+? WHERE product_id=? AND warehouse_id=? AND qty+?>=reserved",{n,pid,wid,n}) ; // check affected rows separately below
 auto q=query("SELECT changes()");q.next();ok=ok && q.value(0).toInt()==1;
 ok=ok && run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason) VALUES(?,?,?,?)",{pid,wid,n,reason->text()});if(ok && db.commit()){refresh();return;}db.rollback();failure("Yetersiz stok veya islem hatasi.");}
 void reserve(){QDialog d(this);auto *f=form(d,"Stok Rezervasyonu");auto *p=new QComboBox;auto *w=new QComboBox;auto *qty=new QSpinBox;auto *hours=new QSpinBox;qty->setRange(1,100000);hours->setRange(1,720);hours->setValue(24);auto q=query("SELECT id,name FROM products");while(q.next())p->addItem(q.value(1).toString(),q.value(0));q=query("SELECT id,name FROM warehouses");while(q.next())w->addItem(q.value(1).toString(),q.value(0));f->addRow("Urun",p);f->addRow("Depo",w);f->addRow("Adet",qty);f->addRow("Saat",hours);if(d.exec()!=QDialog::Accepted)return;if(p->currentIndex()<0 || w->currentIndex()<0)return failure("Urun ve depo ekleyin.");int pid=p->currentData().toInt(),wid=w->currentData().toInt();if(!db.transaction())return failure(err());bool ok=run("UPDATE stock SET reserved=reserved+? WHERE product_id=? AND warehouse_id=? AND qty-reserved>=?",{qty->value(),pid,wid,qty->value()});auto c=query("SELECT changes()");c.next();ok=ok && c.value(0).toInt()==1;ok=ok && run("INSERT INTO reservations(product_id,warehouse_id,qty,expires_at) VALUES(?,?,?,?)",{pid,wid,qty->value(),QDateTime::currentDateTimeUtc().addSecs(hours->value()*3600).toString(Qt::ISODate)});if(ok&&db.commit()){refresh();return;}db.rollback();failure("Stok yetersiz.");}
 void releaseReservation(){bool ok=false;int rid=QInputDialog::getInt(this,"Rezervasyon serbest birak","Rezervasyon ID",1,1,INT_MAX,1,&ok);if(!ok)return;if(!db.transaction())return failure(err());auto q=query("SELECT product_id,warehouse_id,qty FROM reservations WHERE id=? AND status='Aktif'",{rid});if(!q.next()){db.rollback();return failure("Aktif rezervasyon bulunamadi.");}int pid=q.value(0).toInt(),wid=q.value(1).toInt(),qty=q.value(2).toInt();bool success=run("UPDATE stock SET reserved=reserved-? WHERE product_id=? AND warehouse_id=? AND reserved>=?",{qty,pid,wid,qty}) && run("UPDATE reservations SET status='Iptal' WHERE id=? AND status='Aktif'",{rid});if(success&&db.commit()){refresh();return;}db.rollback();failure("Rezervasyon iptal edilemedi.");}
 void newCash(){QDialog d(this);auto *f=form(d,"Kasa Gelir / Gider");auto *kind=new QComboBox;kind->addItems({"Gelir","Gider"});auto *sum=amount();auto *desc=new QLineEdit;f->addRow("Tur",kind);f->addRow("Tutar",sum);f->addRow("Aciklama",desc);if(d.exec()!=QDialog::Accepted)return;if(sum->value()<=0 || desc->text().trimmed().isEmpty())return failure("Tutar ve aciklama gerekli.");qint64 signedValue=toCents(sum->value())*(kind->currentIndex()==0?1:-1);if(!run("INSERT INTO cash_moves(kind,amount_cents,description) VALUES(?,?,?)",{kind->currentText(),signedValue,desc->text()}))return failure("Kasa islemi kaydedilemedi.");refresh();}
 void addCart(){int id=product->currentData().toInt();if(!id)return failure("Once urun ekleyin.");int qty=quantity->value();for(auto &x:cart)if(x.first==id){x.second+=qty;refreshCart();return;}cart.append({id,qty});refreshCart();}
 void refreshCart(){cartTable->setRowCount(0);qint64 total=0;for(const auto &x:cart){auto q=query("SELECT sku,name,price_cents FROM products WHERE id=?",{x.first});if(!q.next())continue;int r=cartTable->rowCount();cartTable->insertRow(r);qint64 p=q.value(2).toLongLong();QStringList vals={q.value(0).toString(),q.value(1).toString(),QString::number(x.second),money(p),money(p*x.second)};for(int c=0;c<vals.size();c++)cartTable->setItem(r,c,new QTableWidgetItem(vals[c]));total+=p*x.second;}cartTable->resizeColumnsToContents();totalLabel->setText("Sepet toplam: "+money(total));}
 void holdCart(){if(cart.isEmpty())return failure("Sepet bos.");bool ok=false;QString label=QInputDialog::getText(this,"Sepet Beklet","Musteri / sepet etiketi",QLineEdit::Normal,"Bekleyen sepet",&ok);if(!ok)return;QJsonArray a;for(auto x:cart){QJsonArray line;line.append(x.first);line.append(x.second);a.append(line);}auto blob=QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));if(!run("INSERT INTO held_carts(label,cart_json) VALUES(?,?)",{label,blob}))return failure("Sepet bekletilemedi.");cart.clear();refreshCart();message("Sepet bekletildi. Bekleyen Sepetler alanindan acabilirsiniz.");}
 void openHeld(){QDialog d(this);auto *v=new QVBoxLayout(&d);auto *list=new QListWidget;auto q=query("SELECT id,label,created_at FROM held_carts ORDER BY id DESC");while(q.next()){auto *item=new QListWidgetItem(QString("#%1 %2 (%3)").arg(q.value(0).toString(),q.value(1).toString(),q.value(2).toString()));item->setData(Qt::UserRole,q.value(0));list->addItem(item);}v->addWidget(list);auto *b=new QPushButton("Secileni sepete getir");v->addWidget(b);connect(b,&QPushButton::clicked,&d,&QDialog::accept);if(d.exec()!=QDialog::Accepted || !list->currentItem())return;if(!cart.isEmpty())return failure("Mevcut sepeti once tamamlayin veya bekletin.");int id=list->currentItem()->data(Qt::UserRole).toInt();auto qq=query("SELECT cart_json FROM held_carts WHERE id=?",{id});if(!qq.next())return;auto a=QJsonDocument::fromJson(qq.value(0).toByteArray()).array();for(auto v:a){auto l=v.toArray();if(l.size()==2)cart.append({l[0].toInt(),l[1].toInt()});}run("DELETE FROM held_carts WHERE id=?",{id});refreshCart();}
 void checkout(const QString &type){if(cart.isEmpty())return failure("Sepet bos.");int wid=warehouse->currentData().toInt();if(!wid)return failure("Depo secin.");if(!db.transaction())return failure(err());bool ok=true;QVector<QPair<int,qint64>> prices; qint64 total=0;
 for(auto x:cart){auto q=query("SELECT price_cents,cost_cents FROM products WHERE id=?",{x.first});if(!q.next()){ok=false;break;}qint64 unit=(type=="Satin Alma")?q.value(1).toLongLong():q.value(0).toLongLong();prices.append({x.first,unit});total+=unit*x.second;
 if(type=="Satis"){auto physical=query("SELECT COUNT(*) FROM carton_stock c JOIN product_packs p ON p.id=c.pack_id WHERE p.product_id=? AND c.warehouse_id=? AND (c.sealed_qty>0 OR c.loose_units>0)",{x.first,wid});if(!physical.next()||physical.value(0).toInt()>0){ok=false;break;}ok=run("UPDATE stock SET qty=qty-? WHERE product_id=? AND warehouse_id=? AND qty-reserved>=?",{x.second,x.first,wid,x.second});auto c=query("SELECT changes()");c.next();ok=ok&&c.value(0).toInt()==1;if(!ok)break;}
 if(type=="Satin Alma"){ok=run("INSERT OR IGNORE INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,?,0,0)",{x.first,wid}) && run("UPDATE stock SET qty=qty+? WHERE product_id=? AND warehouse_id=?",{x.second,x.first,wid});if(!ok)break;}
 }
 bool instant=type=="Satis"||type=="Satin Alma";int party=customer->currentData().toInt();QVariant partyArg=party>0?QVariant(party):QVariant();
 if(ok)ok=run("INSERT INTO docs(doc_type,status,party_id,total_cents) VALUES(?,?,?,?)",{type,instant?"Tamamlandi":"Taslak",partyArg,total});int doc=0;if(ok){auto q=query("SELECT last_insert_rowid()");q.next();doc=q.value(0).toInt();}
 for(int i=0;ok&&i<cart.size();i++){auto x=cart[i];ok=run("INSERT INTO doc_lines(doc_id,product_id,warehouse_id,qty,unit_cents) VALUES(?,?,?,?,?)",{doc,x.first,wid,x.second,prices[i].second});if(instant && ok)ok=run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason,doc_id) VALUES(?,?,?,?,?)",{x.first,wid,type=="Satis"?-x.second:x.second,type,doc});}
 if(ok&&instant){if(type=="Satis" && payment->currentText()=="Nakit")ok=run("INSERT INTO cash_moves(kind,amount_cents,description,doc_id) VALUES('Satis',?,'POS nakit tahsilat',?)",{total,doc});
 if(type=="Satin Alma" && payment->currentText()=="Nakit")ok=run("INSERT INTO cash_moves(kind,amount_cents,description,doc_id) VALUES('Alis',?,'Nakit satin alma',?)",{-total,doc});
 if(payment->currentText()=="Veresiye" && party>0)ok=run("UPDATE parties SET balance_cents=balance_cents+? WHERE id=?",{type=="Satis"?total:-total,party});
 if(payment->currentText()=="Veresiye" && party<=0)ok=false;
 }
 if(ok&&db.commit()){cart.clear();refresh();message(type+" belgesi #"+QString::number(doc)+" kaydedildi.");}else{db.rollback();failure("Islem geri alindi. Stok, cari veya kapali koli takibi kontrol edilmeli. Fiziksel ambalajli urunleri ana POS ekranindan satin.");}
 }
 void exportCsv(){QString file=QFileDialog::getSaveFileName(this,"CSV aktar","kasapro_rapor.csv","CSV (*.csv)");if(file.isEmpty())return;QFile f(file);if(!f.open(QIODevice::WriteOnly|QIODevice::Text))return failure("Dosya yazilamadi.");QTextStream out(&f);out.setEncoding(QStringConverter::Utf8);out<<QChar(0xFEFF);QStringList hs;for(int c=0;c<table->columnCount();c++)hs<<'"'+table->horizontalHeaderItem(c)->text().replace('"',"\"\"")+'"';out<<hs.join(';')<<'\n';for(int r=0;r<table->rowCount();r++){QStringList vals;for(int c=0;c<table->columnCount();c++){QString s=table->item(r,c)?table->item(r,c)->text():"";s.replace('"',"\"\"");vals<<'"'+s+'"';}out<<vals.join(';')<<'\n';}message("CSV kaydedildi.");}
public:
 BackofficeWindow(){setWindowTitle("KasaPro | Enterprise POS ve Depo v0.2");resize(1540,960);
 setStyleSheet(R"QSS(
 QMainWindow,QWidget{background:#f3f6fb;color:#192c48;font-family:'Segoe UI';font-size:12px}
 QLabel#heading{font-size:27px;font-weight:800;color:#102641;padding:8px 2px}
 QLabel#sectionTitle{font-size:15px;font-weight:800;color:#173c63;letter-spacing:1px}
 QLabel#muted{color:#6b7d94;font-size:12px}
 QLabel#productPreview{background:white;border:1px solid #d9e4f2;border-radius:15px;color:#8999ad;font-weight:700}
 QLabel#warehouseSummary{background:#e3edf9;border:1px solid #ccdff2;border-radius:12px;padding:18px;font-size:15px;font-weight:800;color:#123b6c}
 QTabWidget::pane{border:1px solid #dce6f3;background:#fff;border-radius:12px;padding:12px}
 QTabBar::tab{padding:16px 24px;background:#e9eff7;margin-right:5px;border-top-left-radius:10px;border-top-right-radius:10px;font-weight:700;color:#506785}
 QTabBar::tab:selected{background:#173d70;color:#fff}
 QPushButton{background:#184c86;color:white;padding:11px 17px;border:0;border-radius:8px;font-weight:700;min-height:17px}
 QPushButton:hover{background:#2674b5}
 QPushButton:pressed{background:#102d52}
 QTableWidget{background:white;alternate-background-color:#f3f7fc;border:1px solid #e0e7f2;border-radius:9px;gridline-color:#eaf0f8;selection-background-color:#dbeaff;selection-color:#173d70}
 QHeaderView::section{background:#eaf1f9;color:#435e7c;border:none;padding:12px;font-weight:800}
 QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:#fff;padding:10px;border:1px solid #ccd9e9;border-radius:8px;min-height:16px}
 QLineEdit:focus,QComboBox:focus{border:1px solid #277bc3}
 QDialog{background:#f7f9fc}
 QStatusBar{background:#173d70;color:white}
 )QSS");
 auto *central=new QWidget;setCentralWidget(central);auto *base=new QVBoxLayout(central);auto *heading=new QLabel("KasaPro  /  Isletme Yonetim Merkezi");heading->setObjectName("heading");base->addWidget(heading);dashboardLabel=new QLabel;base->addWidget(dashboardLabel);tabs=new QTabWidget;base->addWidget(tabs);
 auto *pos=new QWidget;auto *pv=new QVBoxLayout(pos);auto *controls=new QHBoxLayout;product=new QComboBox;product->setMinimumWidth(260);warehouse=new QComboBox;customer=new QComboBox;payment=new QComboBox;payment->addItems({"Nakit","Kart","Veresiye"});quantity=new QSpinBox;quantity->setRange(1,100000);auto *add=new QPushButton("Sepete Ekle");controls->addWidget(new QLabel("Urun"));controls->addWidget(product,3);controls->addWidget(new QLabel("Depo"));controls->addWidget(warehouse);controls->addWidget(new QLabel("Cari"));controls->addWidget(customer);controls->addWidget(new QLabel("Adet"));controls->addWidget(quantity);controls->addWidget(add);pv->addLayout(controls);
 auto *productStrip=new QHBoxLayout;productPreview=new QLabel("URUN GORSELI YOK");productPreview->setFixedSize(168,150);productPreview->setAlignment(Qt::AlignCenter);productPreview->setObjectName("productPreview");productStrip->addWidget(productPreview);auto *info=new QVBoxLayout;auto *infoHead=new QLabel("SECILI URUN / DEPO STOK BILGISI");infoHead->setObjectName("sectionTitle");info->addWidget(infoHead);availabilityLabel=new QLabel;availabilityLabel->setWordWrap(true);info->addWidget(availabilityLabel);auto *tip=new QLabel("Depo secimini degistirerek satis oncesi rezerve ve kullanilabilir miktari kontrol edin.");tip->setWordWrap(true);tip->setObjectName("muted");info->addWidget(tip);info->addStretch();productStrip->addLayout(info,1);pv->addLayout(productStrip);
 connect(product,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){refreshProductPreview();});connect(warehouse,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){refreshProductPreview();});
 auto *second=new QHBoxLayout;second->addWidget(new QLabel("Odeme"));second->addWidget(payment);second->addStretch();auto *hold=new QPushButton("Sepet Beklet"),*load=new QPushButton("Bekleyen Sepetler"),*clear=new QPushButton("Sepeti Temizle");for(auto *b:{hold,load,clear})second->addWidget(b);pv->addLayout(second);cartTable=new QTableWidget;cartTable->setColumnCount(5);cartTable->setHorizontalHeaderLabels({"Kod","Urun","Adet","Birim fiyat","Toplam"});cartTable->horizontalHeader()->setStretchLastSection(true);pv->addWidget(cartTable);totalLabel=new QLabel;pv->addWidget(totalLabel);auto *actions=new QHBoxLayout;for(auto s:{"Satis","Satin Alma","Teklif","Siparis"}){QString type=QString::fromUtf8(s);auto *b=new QPushButton(type+" Kaydet");actions->addWidget(b);connect(b,&QPushButton::clicked,this,[this,type]{checkout(type);});}pv->addLayout(actions);tabs->addTab(pos,"POS / Satis");connect(add,&QPushButton::clicked,this,[this]{addCart();});connect(hold,&QPushButton::clicked,this,[this]{holdCart();});connect(load,&QPushButton::clicked,this,[this]{openHeld();});connect(clear,&QPushButton::clicked,this,[this]{cart.clear();refreshCart();});
 auto *records=new QWidget;auto *rv=new QVBoxLayout(records);auto *toolbar=new QHBoxLayout; viewSelect=new QComboBox;viewSelect->addItems(views.keys());viewSelect->setMinimumWidth(190);toolbar->addWidget(new QLabel("Liste:"));toolbar->addWidget(viewSelect);auto *party=new QPushButton("+ Cari"),*emp=new QPushButton("+ Personel"),*depot=new QPushButton("+ Depo"),*item=new QPushButton("+ Urun"),*movement=new QPushButton("+ Stok Hareketi"),*reservation=new QPushButton("+ Rezervasyon"),*release=new QPushButton("Rezervasyon Iptal"),*cash=new QPushButton("+ Kasa"),*csv=new QPushButton("CSV");for(auto *b:{party,emp,depot,item,movement,reservation,release,cash,csv})toolbar->addWidget(b);auto *barScroll=new QScrollArea;auto *barWidget=new QWidget;barWidget->setLayout(toolbar);barScroll->setWidget(barWidget);barScroll->setWidgetResizable(true);barScroll->setFixedHeight(66);rv->addWidget(barScroll);table=new QTableWidget;rv->addWidget(table);tabs->addTab(records,"Kayitlar / Raporlar");
 auto *depoPage=new QWidget;auto *depoLayout=new QVBoxLayout(depoPage);auto *depoTop=new QHBoxLayout;auto *depoTitle=new QLabel("DEPO BAZLI STOK KONTROL MERKEZI");depoTitle->setObjectName("sectionTitle");depoTop->addWidget(depoTitle);depoTop->addStretch();depoTop->addWidget(new QLabel("Depo:"));warehouseFilter=new QComboBox;warehouseFilter->setMinimumWidth(220);depoTop->addWidget(warehouseFilter);warehouseSearch=new QLineEdit;warehouseSearch->setPlaceholderText("Urun adi veya stok koduyla ara...");warehouseSearch->setMinimumWidth(290);depoTop->addWidget(warehouseSearch);depoLayout->addLayout(depoTop);warehouseSummary=new QLabel;warehouseSummary->setObjectName("warehouseSummary");depoLayout->addWidget(warehouseSummary);warehouseTable=new QTableWidget;warehouseTable->setAlternatingRowColors(true);warehouseTable->setSelectionBehavior(QAbstractItemView::SelectRows);warehouseTable->setEditTriggers(QAbstractItemView::NoEditTriggers);depoLayout->addWidget(warehouseTable);auto *depoBottom=new QHBoxLayout;auto *depoEntry=new QPushButton("+ Depoya Stok Girisi / Duzeltme");depoBottom->addWidget(depoEntry);depoBottom->addStretch();auto *depoRefresh=new QPushButton("Listeyi Yenile");depoBottom->addWidget(depoRefresh);depoLayout->addLayout(depoBottom);tabs->addTab(depoPage,"Depo Stok Explorer");connect(warehouseFilter,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){refreshWarehouseExplorer();});connect(warehouseSearch,&QLineEdit::textChanged,this,[this]{refreshWarehouseExplorer();});connect(depoEntry,&QPushButton::clicked,this,[this]{stockEntry();});connect(depoRefresh,&QPushButton::clicked,this,[this]{refreshWarehouseExplorer();});connect(viewSelect,&QComboBox::currentTextChanged,this,[this]{showView();});connect(party,&QPushButton::clicked,this,[this]{addParty();});connect(emp,&QPushButton::clicked,this,[this]{addEmployee();});connect(depot,&QPushButton::clicked,this,[this]{addWarehouse();});connect(item,&QPushButton::clicked,this,[this]{addProduct();});connect(movement,&QPushButton::clicked,this,[this]{stockEntry();});connect(reservation,&QPushButton::clicked,this,[this]{reserve();});connect(release,&QPushButton::clicked,this,[this]{releaseReservation();});connect(cash,&QPushButton::clicked,this,[this]{newCash();});connect(csv,&QPushButton::clicked,this,[this]{exportCsv();});
 refresh();statusBar()->showMessage("Yerel SQLite | Veriler kullanici AppData klasorunde saklanir");}
};

// V0.6: user-defined POS layout backed by the SAME SQLite database and backoffice.
#include <QScrollArea>
#include <QGridLayout>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QInputDialog>
#include <QPointer>
#include <functional>

static QString orangeButton(){return "QPushButton{background:#ff542b;color:white;border:0;border-radius:12px;padding:12px;font-weight:700;}QPushButton:hover{background:#e84524;}";}
static QString paleButton(){return "QPushButton{background:white;color:#283341;border:1px solid #e2e8ee;border-radius:14px;padding:12px;font-weight:650;}QPushButton:hover{border:1px solid #ff542b;}";}


// Management workspace deliberately has its own modern UI; legacy BackofficeWindow
// stays in source for compatibility, but is no longer opened from the POS.
class ManagementCenter final : public QDialog {
    QString section;
    QListWidget *nav=nullptr;
    QLabel *heading=nullptr,*subheading=nullptr,*metrics=nullptr;
    QWidget *dashboardCards=nullptr;
    QVector<QLabel*> dashboardValues;
    QLineEdit *filter=nullptr;
    QTableWidget *listing=nullptr;
    QPushButton *primary=nullptr;
    QMap<QString,QPushButton*> moduleActions;
    QScrollArea *actionsArea=nullptr;
    QGridLayout *actionGrid=nullptr;
    QPushButton *fullscreenButton=nullptr;
    bool fullscreenMode=false;
    void toggleFullscreen(){fullscreenMode=!isFullScreen();if(fullscreenMode)showFullScreen();else showNormal();if(fullscreenButton)fullscreenButton->setText(fullscreenMode?"❐ Pencereye Dön":"⛶ Tam Ekran");}
    void updateActions(){
      static const QMap<QString,QStringList> visible={
        {"Genel Bakış",{"Sektör / Ürün Profilleri","Listeyi Yenile","Türkçe Market Verisi"}},
        {"Cari Kartlar",{"Cari Tahsilat / Ödeme","Cari Ekstre","Tedarikçi Açık Borçları","Listeyi Yenile"}},
        {"Ürün Yönetimi",{"Sektör / Ürün Profilleri","Ürün Detayları","Varyant / Beden / Numara","Ambalaj / Koli Merkezi","Kapalı Koli / Açık Adet","Ürünü Düzenle","Türkçe Market Verisi","Listeyi Yenile"}},
        {"Depo / Stok",{"+ Yeni Depo","Depolar Arası Transfer","Stok Sayımı","Sayım Geçmişi","Kesirli Stok Defteri","Kesirli Pilot Alım / Satım","Kesirli Stok Mutabakatı","Ambalaj / Koli Merkezi","Kapalı Koli / Açık Adet","Listeyi Yenile"}},
        {"Stok Hareketleri",{"Depolar Arası Transfer","Sayım Geçmişi","Listeyi Yenile"}},
        {"Rezervasyonlar",{"Listeyi Yenile"}},
        {"Kasa",{"Kasa Dökümü","Cari Tahsilat / Ödeme","Listeyi Yenile"}},
        {"Siparişler",{"Satın Alma Merkezi","Listeyi Yenile"}},
        {"Satın Alma",{"Satın Alma Merkezi","Tedarikçi Açık Borçları","Listeyi Yenile"}},
        {"Personel",{"Listeyi Yenile"}},
        {"Raporlar",{"Kasa Dökümü","Cari Ekstre","Listeyi Yenile"}}
      };
      const QStringList allowed=visible.value(section,QStringList{"Listeyi Yenile"});
      // Sprint 01: compact visible actions into consecutive cells; do not leave holes.
      if(actionGrid){
          while(actionGrid->count())actionGrid->takeAt(0);
          const int availableWidth=actionsArea ? actionsArea->viewport()->width() : 760;
          const int columns=qBound(1, availableWidth/205, 4);
          int position=0;
          for(const QString &label:allowed){
              QPushButton *button=moduleActions.value(label,nullptr);
              if(!button)continue;
              button->setVisible(true);
              button->setMinimumWidth(0);
              button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
              actionGrid->addWidget(button,position/columns,position%columns);
              ++position;
          }
          for(auto it=moduleActions.begin();it!=moduleActions.end();++it)
              if(!allowed.contains(it.key()))it.value()->hide();
          const int rows=(position+columns-1)/columns;
          if(actionsArea){
              actionsArea->setVisible(position>0);
              const int needed=qMax(56, rows*54+12);
              actionsArea->setFixedHeight(qMin(needed,230));
          }
      }
    }
    const QStringList modules={"Genel Bakış","Cari Kartlar","Ürün Yönetimi","Depo / Stok","Stok Hareketleri","Rezervasyonlar","Kasa","Siparişler","Satın Alma","Personel","Raporlar"};
    static QString fmt(qint64 cents){return money(cents);}
    static bool execute(const QString &sql,const QVariantList &args={}){QString error; if(!run(sql,args,&error)){QMessageBox::warning(nullptr,"Veritabanı",error);return false;}return true;}
    void updateTable(QSqlQuery q){
        listing->clear();listing->setRowCount(0);
        QSqlRecord rec=q.record();listing->setColumnCount(rec.count());QStringList cols;
        for(int j=0;j<rec.count();++j)cols<<rec.fieldName(j);
        listing->setHorizontalHeaderLabels(cols);
        while(q.next()){int r=listing->rowCount();listing->insertRow(r);for(int c=0;c<rec.count();++c){auto *item=new QTableWidgetItem(q.value(c).toString());item->setFlags(item->flags()&~Qt::ItemIsEditable);listing->setItem(r,c,item);}}
        UiDesign::polishTable(listing);
        listing->verticalHeader()->setVisible(false);
        listing->setWordWrap(false);
        listing->horizontalHeader()->setStretchLastSection(true);
        listing->horizontalHeader()->setMinimumSectionSize(92);
        listing->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        listing->resizeColumnsToContents();
        for(int c=0;c<listing->columnCount();++c)
            listing->setColumnWidth(c,qBound(100,listing->columnWidth(c)+20,280));
    }
    QString esc(const QString &value){return "%"+value+"%";}
    void refresh(){
        QString term=esc(filter->text().trimmed());
        QString sql;QVariantList args;
        if(section=="Genel Bakış"){
            sql="SELECT d.id AS Belge,d.created_at AS Tarih,COALESCE(p.name,'Perakende') AS Musteri,d.total_cents/100.0 AS Tutar_TL FROM docs d LEFT JOIN parties p ON p.id=d.party_id WHERE d.doc_type='Satis' AND d.status='Tamamlandi' ORDER BY d.id DESC LIMIT 50";
        }else if(section=="Cari Kartlar"){
            sql="SELECT id AS ID,name AS Cari,kind AS Tur,phone AS Telefon,tax_no AS Vergi_No,balance_cents/100.0 AS Bakiye_TL FROM parties WHERE name LIKE ? OR phone LIKE ? ORDER BY id DESC";args={term,term};
        }else if(section=="Ürün Yönetimi"){
            sql="SELECT id AS ID,sku AS Kod,name AS Urun,price_cents/100.0 AS Satis_TL,cost_cents/100.0 AS Maliyet_TL,minimum_qty AS Kritik_Stok,image_path AS Fotograf FROM products WHERE sku LIKE ? OR name LIKE ? ORDER BY id DESC";args={term,term};
        }else if(section=="Depo / Stok"){
            sql="SELECT w.name AS Depo,p.sku AS Kod,p.name AS Urun,COALESCE(s.qty,0) AS Fiziki,COALESCE(s.reserved,0) AS Rezerve,COALESCE(s.qty-s.reserved,0) AS Kullanilabilir FROM warehouses w CROSS JOIN products p LEFT JOIN stock s ON s.warehouse_id=w.id AND s.product_id=p.id WHERE w.name LIKE ? OR p.name LIKE ? OR p.sku LIKE ? ORDER BY w.name,p.name";args={term,term,term};
        }else if(section=="Stok Hareketleri"){
            sql="SELECT m.id AS ID,m.created_at AS Tarih,w.name AS Depo,p.name AS Urun,m.delta_qty AS Miktar,m.reason AS Sebep FROM stock_moves m JOIN products p ON p.id=m.product_id JOIN warehouses w ON w.id=m.warehouse_id WHERE p.name LIKE ? OR m.reason LIKE ? ORDER BY m.id DESC";args={term,term};
        }else if(section=="Rezervasyonlar"){
            sql="SELECT r.id AS ID,p.name AS Urun,w.name AS Depo,r.qty AS Adet,r.status AS Durum,r.expires_at AS Bitis FROM reservations r JOIN products p ON p.id=r.product_id JOIN warehouses w ON w.id=r.warehouse_id WHERE p.name LIKE ? ORDER BY r.id DESC";args={term};
        }else if(section=="Kasa"){
            sql="SELECT id AS ID,kind AS Tur,amount_cents/100.0 AS Tutar_TL,description AS Aciklama,created_at AS Tarih FROM cash_moves WHERE description LIKE ? OR kind LIKE ? ORDER BY id DESC";args={term,term};
        }else if(section=="Siparişler"||section=="Satın Alma"){
            sql="SELECT d.id AS ID,d.doc_type AS Belge,d.status AS Durum,COALESCE(p.name,'-') AS Cari,d.total_cents/100.0 AS Toplam_TL,d.created_at AS Tarih FROM docs d LEFT JOIN parties p ON p.id=d.party_id WHERE d.doc_type=? AND (p.name LIKE ? OR CAST(d.id AS TEXT) LIKE ?) ORDER BY d.id DESC";args={section=="Siparişler"?"Siparis":"Satin Alma",term,term};
        }else if(section=="Personel"){
            sql="SELECT id AS ID,name AS Ad_Soyad,role AS Gorev,salary_cents/100.0 AS Maas_TL,active AS Aktif FROM employees WHERE name LIKE ? OR role LIKE ? ORDER BY id DESC";args={term,term};
        }else{
            sql="SELECT p.sku AS Kod,p.name AS Urun,COALESCE(SUM(l.qty),0) AS Satis_Adedi,COALESCE(SUM(l.qty*l.unit_cents),0)/100.0 AS Ciro_TL FROM products p LEFT JOIN doc_lines l ON l.product_id=p.id AND l.doc_id IN (SELECT id FROM docs WHERE doc_type='Satis' AND status='Tamamlandi') WHERE p.name LIKE ? GROUP BY p.id ORDER BY Ciro_TL DESC";args={term};
        }
        updateTable(query(sql,args));
        auto summary=query("SELECT (SELECT COUNT(*) FROM products),(SELECT COUNT(*) FROM parties),(SELECT COALESCE(SUM(qty-reserved),0) FROM stock),(SELECT COALESCE(SUM(amount_cents),0) FROM cash_moves),(SELECT COALESCE(SUM(total_cents),0) FROM docs WHERE doc_type='Satis' AND status='Tamamlandi')");
        if(summary.next())metrics->setText(QString("ÜRÜN  %1     CARİ  %2     KULLANILABİLİR STOK  %3     KASA  %4     SATIŞ  %5").arg(summary.value(0).toString(),summary.value(1).toString(),summary.value(2).toString(),fmt(summary.value(3).toLongLong()),fmt(summary.value(4).toLongLong())));
        if(dashboardValues.size()==6){
            auto kpi=query("SELECT "
              "(SELECT COALESCE(SUM(total_cents),0) FROM docs WHERE doc_type='Satis' AND status='Tamamlandi' AND date(created_at,'localtime')=date('now','localtime')) AS daily_sales,"
              "(SELECT COALESCE(SUM(amount_cents),0) FROM cash_moves) AS cash_balance,"
              "(SELECT COUNT(*) FROM products) AS products,"
              "(SELECT COUNT(*) FROM parties) AS parties,"
              "(SELECT COUNT(*) FROM stock s JOIN products p ON p.id=s.product_id WHERE s.qty-s.reserved<=p.minimum_qty) AS critical,"
              "(SELECT COUNT(*) FROM docs WHERE doc_type='Satin Alma' AND status='Taslak') AS pending_orders");
            if(kpi.next()){
                dashboardValues[0]->setText(fmt(kpi.value(0).toLongLong()));
                dashboardValues[1]->setText(fmt(kpi.value(1).toLongLong()));
                for(int i=2;i<6;++i)dashboardValues[i]->setText(kpi.value(i).toString());
            }
        }
        subheading->setText(QString("%1 kayıt görüntüleniyor  •  Canlı SQLite verisi").arg(listing->rowCount()));
    }
    void addParty(){QDialog dialog(this);dialog.setWindowTitle("Yeni cari kart");auto *f=new QFormLayout(&dialog);auto *name=new QLineEdit;auto *kind=new QComboBox;kind->addItems({"Musteri","Tedarikci","Her ikisi"});auto *phone=new QLineEdit;auto *tax=new QLineEdit;f->addRow("Cari unvanı",name);f->addRow("Cari türü",kind);f->addRow("Telefon",phone);f->addRow("Vergi / TC no",tax);auto *ok=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);f->addRow(ok);connect(ok,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(ok,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return;if(name->text().trimmed().isEmpty())return;if(execute("INSERT INTO parties(name,kind,phone,tax_no) VALUES(?,?,?,?)",{name->text().trimmed(),kind->currentText(),phone->text(),tax->text()}))refresh();}
    void addProduct(){QDialog dialog(this);dialog.setWindowTitle("Yeni ürün ve fotoğraf");auto *f=new QFormLayout(&dialog);auto *sku=new QLineEdit;auto *barcode=new QLineEdit;barcode->setMaxLength(64);barcode->setPlaceholderText("EAN-13 / EAN-8 / UPC / Code128");auto *name=new QLineEdit;auto *price=new QDoubleSpinBox;price->setRange(0,100000000);price->setDecimals(2);auto *cost=new QDoubleSpinBox;cost->setRange(0,100000000);cost->setDecimals(2);auto *minimum=new QSpinBox;minimum->setRange(0,1000000);auto *imagePath=new QLineEdit;imagePath->setReadOnly(true);auto *choose=new QPushButton("Fotoğraf seç");f->addRow("Stok kodu",sku);f->addRow("Barkod",barcode);f->addRow("Ürün adı",name);f->addRow("Satış fiyatı TL",price);f->addRow("Alış maliyeti TL",cost);f->addRow("Kritik miktar",minimum);f->addRow("Fotoğraf",imagePath);f->addRow(choose);connect(choose,&QPushButton::clicked,&dialog,[&]{auto path=QFileDialog::getOpenFileName(&dialog,"Görsel seç",QString(),"Resim (*.png *.jpg *.jpeg *.webp)");if(!path.isEmpty())imagePath->setText(path);});auto *ok=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);f->addRow(ok);connect(ok,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(ok,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted||sku->text().trimmed().isEmpty()||name->text().trimmed().isEmpty())return;QString stored;if(!imagePath->text().isEmpty()){auto dir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/product_images";QDir().mkpath(dir);stored=dir+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+"."+QFileInfo(imagePath->text()).suffix();if(!QFile::copy(imagePath->text(),stored)){QMessageBox::warning(this,"Fotoğraf","Dosya kopyalanamadı");return;}}if(execute("INSERT INTO products(sku,barcode,name,price_cents,cost_cents,minimum_qty,image_path) VALUES(?,?,?,?,?,?,?)",{sku->text().trimmed(),barcode->text().trimmed(),name->text().trimmed(),qRound64(price->value()*100),qRound64(cost->value()*100),minimum->value(),stored}))refresh();}
    void addWarehouse(){bool accepted=false;auto name=QInputDialog::getText(this,"Yeni depo","Depo adı",QLineEdit::Normal,QString(),&accepted);if(accepted&&!name.trimmed().isEmpty()&&execute("INSERT INTO warehouses(name) VALUES(?)",{name.trimmed()}))refresh();}
    void stockEntry(){QDialog dialog(this);dialog.setWindowTitle("Stok giriş / düzeltme");auto *f=new QFormLayout(&dialog);auto *products=new QComboBox;auto *warehouses=new QComboBox;auto *qty=new QSpinBox;qty->setRange(-1000000,1000000);qty->setValue(1);auto *reason=new QLineEdit("Sayım / stok düzeltme");auto productsQuery=query("SELECT id,name FROM products ORDER BY name");while(productsQuery.next())products->addItem(productsQuery.value(1).toString(),productsQuery.value(0));auto whQuery=query("SELECT id,name FROM warehouses ORDER BY name");while(whQuery.next())warehouses->addItem(whQuery.value(1).toString(),whQuery.value(0));f->addRow("Ürün",products);f->addRow("Depo",warehouses);f->addRow("Miktar (+/-)",qty);f->addRow("Gerekçe",reason);auto *ok=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);f->addRow(ok);connect(ok,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(ok,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted||products->currentIndex()<0||warehouses->currentIndex()<0||qty->value()==0)return;int pid=products->currentData().toInt();int wid=warehouses->currentData().toInt();int delta=qty->value();if(!db.transaction())return;bool success=run("INSERT OR IGNORE INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,?,0,0)",{pid,wid})&&run("UPDATE stock SET qty=qty+? WHERE product_id=? AND warehouse_id=? AND qty+?>=reserved",{delta,pid,wid,delta});auto c=query("SELECT changes()");success=success&&c.next()&&c.value(0).toInt()==1;if(success)success=run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason) VALUES(?,?,?,?)",{pid,wid,delta,reason->text()});if(success&&db.commit())refresh();else{db.rollback();QMessageBox::warning(this,"Stok","Stok yetersiz veya kayıt başarısız");}}
    void addCash(){QDialog dialog(this);dialog.setWindowTitle("Kasa gelir / gider");auto *f=new QFormLayout(&dialog);auto *kind=new QComboBox;kind->addItems({"Gelir","Gider"});auto *amount=new QDoubleSpinBox;amount->setRange(0.01,100000000);amount->setDecimals(2);auto *desc=new QLineEdit;f->addRow("İşlem",kind);f->addRow("Tutar TL",amount);f->addRow("Açıklama",desc);auto *ok=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);f->addRow(ok);connect(ok,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(ok,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted||desc->text().trimmed().isEmpty())return;qint64 cents=qRound64(amount->value()*100)*(kind->currentIndex()==0?1:-1);if(execute("INSERT INTO cash_moves(kind,amount_cents,description) VALUES(?,?,?)",{kind->currentText(),cents,desc->text().trimmed()}))refresh();}
    void addEmployee(){QDialog dialog(this);dialog.setWindowTitle("Yeni personel");auto *f=new QFormLayout(&dialog);auto *name=new QLineEdit;auto *role=new QLineEdit;auto *salary=new QDoubleSpinBox;salary->setRange(0,100000000);f->addRow("Ad soyad",name);f->addRow("Görev",role);f->addRow("Maaş TL",salary);auto *ok=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);f->addRow(ok);connect(ok,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(ok,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted||name->text().trimmed().isEmpty())return;if(execute("INSERT INTO employees(name,role,salary_cents) VALUES(?,?,?)",{name->text().trimmed(),role->text().trimmed(),qRound64(salary->value()*100)}))refresh();}
    int selectedId() const {
        if(!listing || listing->currentRow()<0 || !listing->item(listing->currentRow(),0))return 0;
        return listing->item(listing->currentRow(),0)->text().toInt();
    }
    void editProduct(){
        if(section!="Ürün Yönetimi")return;
        int id=selectedId();if(id<=0){QMessageBox::information(this,"Ürün","Düzenlenecek ürünü tablodan seçiniz.");return;}
        auto q=query("SELECT sku,name,price_cents,cost_cents,minimum_qty,image_path,barcode FROM products WHERE id=?",{id});
        if(!q.next())return;
        QDialog d(this);d.setWindowTitle("Ürün Kartı Düzenle");d.resize(530,410);auto *f=new QFormLayout(&d);
        auto *sku=new QLineEdit(q.value(0).toString());auto *barcode=new QLineEdit(q.value(6).toString());barcode->setMaxLength(64);auto *name=new QLineEdit(q.value(1).toString());
        auto *price=new QDoubleSpinBox;price->setRange(0,100000000);price->setDecimals(2);price->setValue(q.value(2).toLongLong()/100.0);
        auto *cost=new QDoubleSpinBox;cost->setRange(0,100000000);cost->setDecimals(2);cost->setValue(q.value(3).toLongLong()/100.0);
        auto *minimum=new QSpinBox;minimum->setRange(0,1000000);minimum->setValue(q.value(4).toInt());
        auto *path=new QLineEdit(q.value(5).toString());path->setReadOnly(true);
        auto *browse=new QPushButton("Fotoğraf Değiştir");
        connect(browse,&QPushButton::clicked,&d,[&]{QString picked=QFileDialog::getOpenFileName(&d,"Ürün fotoğrafı",QString(),"Resim (*.png *.jpg *.jpeg *.webp *.bmp)");if(!picked.isEmpty())path->setText(picked);});
        f->addRow("Stok kodu",sku);f->addRow("Barkod (EAN / UPC / Code128)",barcode);f->addRow("Ürün adı",name);f->addRow("Satış fiyatı TL",price);f->addRow("Maliyet TL",cost);f->addRow("Kritik stok",minimum);f->addRow("Görsel yolu",path);f->addRow(browse);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);f->addRow(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        if(d.exec()!=QDialog::Accepted)return;
        if(sku->text().trimmed().isEmpty()||name->text().trimmed().isEmpty()){QMessageBox::warning(this,"Ürün","Kod ve ürün adı zorunlu.");return;}
        QString image=path->text();QString original=q.value(5).toString();
        if(!image.isEmpty()&&image!=original){QString folder=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/product_images";QDir().mkpath(folder);QString dest=folder+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+"."+QFileInfo(image).suffix().toLower();if(!QFile::copy(image,dest)){QMessageBox::warning(this,"Görsel","Fotoğraf kopyalanamadı.");return;}image=dest;}
        if(execute("UPDATE products SET sku=?,barcode=?,name=?,price_cents=?,cost_cents=?,minimum_qty=?,image_path=? WHERE id=?",{sku->text().trimmed(),barcode->text().trimmed(),name->text().trimmed(),qRound64(price->value()*100),qRound64(cost->value()*100),minimum->value(),image,id}))refresh();
    }
    void transferStock(){
        QDialog d(this);d.setWindowTitle("Depolar Arası Stok Transferi");d.resize(510,330);auto *f=new QFormLayout(&d);
        auto *productBox=new QComboBox;auto *from=new QComboBox;auto *to=new QComboBox;auto *amount=new QSpinBox;amount->setRange(1,1000000);
        auto pq=query("SELECT id,sku||' - '||name FROM products ORDER BY name");while(pq.next())productBox->addItem(pq.value(1).toString(),pq.value(0));
        auto wq=query("SELECT id,name FROM warehouses ORDER BY name");while(wq.next()){from->addItem(wq.value(1).toString(),wq.value(0));to->addItem(wq.value(1).toString(),wq.value(0));}
        if(to->count()>1)to->setCurrentIndex(1);
        f->addRow("Ürün",productBox);f->addRow("Kaynak depo",from);f->addRow("Hedef depo",to);f->addRow("Miktar",amount);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);f->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        if(d.exec()!=QDialog::Accepted)return;
        int pid=productBox->currentData().toInt(),source=from->currentData().toInt(),target=to->currentData().toInt(),n=amount->value();
        if(!pid||!source||!target||source==target){QMessageBox::warning(this,"Transfer","İki farklı depo ve bir ürün seçin.");return;}
        if(!db.transaction()){QMessageBox::warning(this,"Transfer",db.lastError().text());return;}
        bool ok=run("UPDATE stock SET qty=qty-? WHERE product_id=? AND warehouse_id=? AND qty-reserved>=?",{n,pid,source,n});
        auto changed=query("SELECT changes()");ok=ok&&changed.next()&&changed.value(0).toInt()==1;
        if(ok)ok=run("INSERT OR IGNORE INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,?,0,0)",{pid,target});
        if(ok)ok=run("UPDATE stock SET qty=qty+? WHERE product_id=? AND warehouse_id=?",{n,pid,target});
        const QString token=QUuid::createUuid().toString(QUuid::WithoutBraces);
        if(ok)ok=run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason) VALUES(?,?,?,?)",{pid,source,-n,"TRANSFER/"+token+" çıkış"});
        if(ok)ok=run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason) VALUES(?,?,?,?)",{pid,target,n,"TRANSFER/"+token+" giriş"});
        if(ok&&db.commit()){refresh();QMessageBox::information(this,"Transfer","Transfer kaydedildi.");}
        else{db.rollback();QMessageBox::warning(this,"Transfer","Yetersiz kullanılabilir stok veya veritabanı hatası. İşlem geri alındı.");}
    }
    void cashParty(){
        QDialog d(this);d.setWindowTitle("Cari Tahsilat / Ödeme");d.resize(520,320);auto *f=new QFormLayout(&d);
        auto *party=new QComboBox;auto pq=query("SELECT id,name,kind FROM parties ORDER BY name");while(pq.next())party->addItem(pq.value(1).toString()+" · "+pq.value(2).toString(),pq.value(0));
        auto *kind=new QComboBox;kind->addItems({"Müşteri Tahsilatı","Tedarikçi Ödemesi"});
        auto *amount=new QDoubleSpinBox;amount->setDecimals(2);amount->setRange(0.01,100000000);amount->setSuffix(" TL");
        auto *memo=new QLineEdit;f->addRow("Cari",party);f->addRow("İşlem",kind);f->addRow("Tutar",amount);f->addRow("Açıklama",memo);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);f->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        if(d.exec()!=QDialog::Accepted||party->currentIndex()<0)return;
        int id=party->currentData().toInt();qint64 cents=qRound64(amount->value()*100);bool receive=kind->currentIndex()==0;
        if(!db.transaction()){QMessageBox::warning(this,"Cari",db.lastError().text());return;}
        bool ok=run("UPDATE parties SET balance_cents=balance_cents-? WHERE id=?",{receive?cents:-cents,id});
        if(ok)ok=run("INSERT INTO cash_moves(kind,amount_cents,description) VALUES(?,?,?)",{receive?"Cari Tahsilat":"Tedarikçi Ödeme",receive?cents:-cents,QString("Cari #%1 · %2").arg(id).arg(memo->text().trimmed())});
        if(ok)ok=run("INSERT INTO party_movements(party_id,kind,amount_cents,description) VALUES(?,?,?,?)",{id,kind->currentText(),receive?-cents:cents,memo->text().trimmed()});
        if(ok&&db.commit())refresh();else{db.rollback();QMessageBox::warning(this,"Cari","İşlem geri alındı.");}
    }
    void showPartyHistory(){
        if(section!="Cari Kartlar")return;int id=selectedId();if(!id){QMessageBox::information(this,"Cari","Önce cari seçiniz.");return;}
        QDialog d(this);d.setWindowTitle("Cari Hareketleri #"+QString::number(id));d.resize(770,480);auto *v=new QVBoxLayout(&d);
        auto *t=new QTableWidget;v->addWidget(t);auto q=query("SELECT created_at AS Tarih,kind AS Islem,amount_cents/100.0 AS Tutar_TL,description AS Aciklama FROM party_movements WHERE party_id=? ORDER BY id DESC",{id});
        QSqlRecord rec=q.record();t->setColumnCount(rec.count());QStringList h;for(int i=0;i<rec.count();++i)h<<rec.fieldName(i);t->setHorizontalHeaderLabels(h);
        while(q.next()){int r=t->rowCount();t->insertRow(r);for(int c=0;c<rec.count();++c)t->setItem(r,c,new QTableWidgetItem(q.value(c).toString()));}
        t->horizontalHeader()->setStretchLastSection(true);auto *close=new QPushButton("Kapat");v->addWidget(close);connect(close,&QPushButton::clicked,&d,&QDialog::accept);d.exec();
    }
    void stockCountDialog(){
        QDialog dialog(this);dialog.setWindowTitle("Depo Stok Sayımı / Fark Fişi");dialog.resize(570,360);
        auto *layout=new QFormLayout(&dialog);
        auto *depot=new QComboBox;auto *item=new QComboBox;auto *physical=new QLabel("-");
        auto *counted=new QSpinBox;counted->setRange(0,100000000);
        auto *reason=new QLineEdit("Fiziki sayım");
        QSqlQuery warehousesQuery=query("SELECT id,name FROM warehouses ORDER BY name");
        while(warehousesQuery.next())depot->addItem(warehousesQuery.value(1).toString(),warehousesQuery.value(0));
        QSqlQuery productsQuery=query("SELECT id,sku||' - '||name FROM products ORDER BY name");
        while(productsQuery.next())item->addItem(productsQuery.value(1).toString(),productsQuery.value(0));
        auto update=[&](){QSqlQuery stockQuery=query("SELECT qty,reserved FROM stock WHERE warehouse_id=? AND product_id=?",{depot->currentData(),item->currentData()});int qty=0,res=0;if(stockQuery.next()){qty=stockQuery.value(0).toInt();res=stockQuery.value(1).toInt();}physical->setText(QString("Fiziki: %1  |  Rezerve: %2  |  Kullanılabilir: %3").arg(qty).arg(res).arg(qty-res));counted->setValue(qty);};
        connect(depot,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){update();});
        connect(item,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){update();});
        layout->addRow("Depo",depot);layout->addRow("Ürün",item);layout->addRow("Sistem",physical);layout->addRow("Sayılan miktar",counted);layout->addRow("Gerekçe",reason);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);layout->addRow(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(depot->count()==0||item->count()==0)return;update();if(dialog.exec()!=QDialog::Accepted)return;
        const int wid=depot->currentData().toInt();const int pid=item->currentData().toInt();const int actual=counted->value();
        if(reason->text().trimmed().isEmpty()){QMessageBox::warning(this,"Sayım","Gerekçe giriniz.");return;}
        if(!db.transaction()){QMessageBox::warning(this,"Sayım",db.lastError().text());return;}
        bool ok=run("INSERT OR IGNORE INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,?,0,0)",{pid,wid});
        QSqlQuery previous=query("SELECT qty,reserved FROM stock WHERE product_id=? AND warehouse_id=?",{pid,wid});
        if(!previous.next())ok=false;
        const int before=ok?previous.value(0).toInt():0;
        const int reserved=ok?previous.value(1).toInt():0;
        const int delta=actual-before;
        if(actual<reserved)ok=false;
        if(ok)ok=run("UPDATE stock SET qty=? WHERE product_id=? AND warehouse_id=? AND qty=? AND reserved<=?",{actual,pid,wid,before,actual});
        QSqlQuery changed=query("SELECT changes()");if(!changed.next()||changed.value(0).toInt()!=1)ok=false;
        if(ok)ok=run("INSERT INTO stock_counts(warehouse_id,product_id,system_qty,counted_qty,delta_qty,reason) VALUES(?,?,?,?,?,?)",{wid,pid,before,actual,delta,reason->text().trimmed()});
        if(ok&&delta!=0)ok=run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason) VALUES(?,?,?,?)",{pid,wid,delta,"Sayım: "+reason->text().trimmed()});
        if(ok&&db.commit()){refresh();QMessageBox::information(this,"Sayım",QString("Sayım kaydedildi. Fark: %1").arg(delta));}
        else{db.rollback();QMessageBox::warning(this,"Sayım","Sayım kaydedilemedi. Rezerve stoktan düşük sayım veya veritabanı hatası.");}
    }
    void cashAuditDialog(){
        QDialog d(this);d.setWindowTitle("Kasa Kontrol / Hareket Dökümü");d.resize(870,620);
        auto *layout=new QVBoxLayout(&d);
        auto *summary=new QLabel;summary->setStyleSheet("font-size:19px;font-weight:800;color:#0e8276;padding:14px;background:#edf8f5;border-radius:10px;");
        layout->addWidget(summary);
        auto *dateBar=new QHBoxLayout;auto *start=new QDateEdit(QDate::currentDate());auto *end=new QDateEdit(QDate::currentDate());start->setCalendarPopup(true);end->setCalendarPopup(true);dateBar->addWidget(new QLabel("Başlangıç"));dateBar->addWidget(start);dateBar->addWidget(new QLabel("Bitiş"));dateBar->addWidget(end);layout->addLayout(dateBar);
        auto *table=new QTableWidget;table->setColumnCount(5);table->setHorizontalHeaderLabels({"ID","Tarih","Tür","Tutar","Açıklama"});table->setEditTriggers(QAbstractItemView::NoEditTriggers);layout->addWidget(table);
        auto reload=[&](){table->setRowCount(0);qint64 period=0;QSqlQuery q=query("SELECT id,created_at,kind,amount_cents,description FROM cash_moves WHERE date(created_at)>=? AND date(created_at)<=? ORDER BY id DESC",{start->date().toString(Qt::ISODate),end->date().toString(Qt::ISODate)});while(q.next()){int row=table->rowCount();table->insertRow(row);qint64 amount=q.value(3).toLongLong();period+=amount;QStringList values={q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),money(amount),q.value(4).toString()};for(int col=0;col<values.size();++col)table->setItem(row,col,new QTableWidgetItem(values[col]));}QSqlQuery all=query("SELECT COALESCE(SUM(amount_cents),0) FROM cash_moves");qint64 balance=0;if(all.next())balance=all.value(0).toLongLong();summary->setText("Dönem net hareketi: "+money(period)+"     |     Kayıtlı toplam kasa: "+money(balance));table->horizontalHeader()->setStretchLastSection(true);};
        connect(start,&QDateEdit::dateChanged,&d,[&](const QDate &){reload();});connect(end,&QDateEdit::dateChanged,&d,[&](const QDate &){reload();});reload();
        auto *close=new QPushButton("Kapat");layout->addWidget(close);connect(close,&QPushButton::clicked,&d,&QDialog::accept);d.exec();
    }
    void stockCountHistory(){
        QDialog d(this);d.setWindowTitle("Stok Sayım Geçmişi");d.resize(900,520);auto *layout=new QVBoxLayout(&d);auto *table=new QTableWidget;layout->addWidget(table);
        updateTableFor(table,query("SELECT c.id AS ID,c.created_at AS Tarih,w.name AS Depo,p.name AS Urun,c.system_qty AS Sistem,c.counted_qty AS Sayilan,c.delta_qty AS Fark,c.reason AS Gerekce FROM stock_counts c JOIN warehouses w ON w.id=c.warehouse_id JOIN products p ON p.id=c.product_id ORDER BY c.id DESC LIMIT 500"));
        auto *close=new QPushButton("Kapat");layout->addWidget(close);connect(close,&QPushButton::clicked,&d,&QDialog::accept);d.exec();
    }
    static void updateTableFor(QTableWidget *table,QSqlQuery queryResult){QSqlRecord rec=queryResult.record();table->setColumnCount(rec.count());QStringList titles;for(int i=0;i<rec.count();++i)titles<<rec.fieldName(i);table->setHorizontalHeaderLabels(titles);while(queryResult.next()){int row=table->rowCount();table->insertRow(row);for(int col=0;col<rec.count();++col)table->setItem(row,col,new QTableWidgetItem(queryResult.value(col).toString()));}table->horizontalHeader()->setStretchLastSection(true);}
    void marketDemo(){
      const auto reply=QMessageBox::question(this,"Türkçe market demo ürünleri","Örnek market ürünleri Ana Depo'ya eklensin mi?\nBu işlem mevcut ürün fiyatlarını ve stoklarını değiştirmez.");
      if(reply!=QMessageBox::Yes)return;
      QString error;
      if(!installMarketDemo(&error)){QMessageBox::warning(this,"Market örnekleri",error);return;}
      QMessageBox::information(this,"Market örnekleri","Türkçe market ürünleri yüklendi. Ürünler POS ekranında görünecek.");refresh();
    }
    void supplierPayablesDialog(){
        QDialog dlg(this);dlg.setWindowTitle("Tedarikçi Borçları | Fatura Bazlı Ödeme");dlg.resize(1050,680);
        auto *layout=new QVBoxLayout(&dlg);
        auto *info=new QLabel("Mal kabulde oluşan belgeler listelenir. Önceki sürümlerdeki borçlar otomatik bağlanmaz. Ödeme ilgili belgeye kaydedilir.");info->setWordWrap(true);layout->addWidget(info);
        auto *header=new QHBoxLayout;
        auto *supplier=new QComboBox;supplier->addItem("Tüm tedarikçiler",0);
        QSqlQuery suppliers=query("SELECT id,name FROM parties WHERE kind IN ('Tedarikci','Her ikisi') ORDER BY name");
        while(suppliers.next())supplier->addItem(suppliers.value(1).toString(),suppliers.value(0));
        auto *status=new QComboBox;status->addItems({"Açık borçlar","Tüm belgeler","Vadesi geçmiş"});
        header->addWidget(new QLabel("Tedarikçi:"));header->addWidget(supplier,2);header->addWidget(new QLabel("Filtre:"));header->addWidget(status);layout->addLayout(header);
        auto *summary=new QLabel;summary->setStyleSheet("font-size:17px;font-weight:700;padding:12px;background:#e9f7f5;border-radius:9px;");layout->addWidget(summary);
        auto *table=new QTableWidget;table->setColumnCount(8);table->setHorizontalHeaderLabels({"Borç ID","Tedarikçi","Sipariş","Belge","Tutar","Ödenen","Kalan","Vade"});table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setAlternatingRowColors(true);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(table,1);
        auto reload=[&](){
            table->setRowCount(0);qint64 original=0,paid=0;int count=0;
            QSqlQuery q=query("SELECT b.id,p.name,b.purchase_order_id,b.doc_id,b.original_cents,b.paid_cents,b.due_date,b.supplier_id FROM supplier_payables b JOIN parties p ON p.id=b.supplier_id WHERE (?=0 OR b.supplier_id=?) AND (?=1 OR b.paid_cents<b.original_cents) AND (?!=2 OR (b.paid_cents<b.original_cents AND b.due_date<date('now','localtime'))) ORDER BY b.due_date,b.id",{supplier->currentData(),supplier->currentData(),status->currentIndex(),status->currentIndex()});
            while(q.next()){
                int row=table->rowCount();table->insertRow(row);const qint64 o=q.value(4).toLongLong(),a=q.value(5).toLongLong();original+=o;paid+=a;++count;
                QStringList values={q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),money(o),money(a),money(o-a),q.value(6).toString()};
                for(int col=0;col<values.size();++col){auto *item=new QTableWidgetItem(values[col]);if(col==6&&o>a&&q.value(6).toString()<QDate::currentDate().toString(Qt::ISODate))item->setForeground(QColor("#b91c1c"));table->setItem(row,col,item);}
            }
            summary->setText(QString("%1 belge  |  Toplam %2  |  Ödenen %3  |  Kalan %4").arg(count).arg(money(original)).arg(money(paid)).arg(money(original-paid)));
        };
        auto *buttons=new QHBoxLayout;auto *pay=new QPushButton("Seçilen Belgeye Ödeme Yap");auto *history=new QPushButton("Ödeme Geçmişi");auto *due=new QPushButton("Vade Düzenle");auto *reloadButton=new QPushButton("Yenile");buttons->addWidget(pay);buttons->addWidget(history);buttons->addWidget(due);buttons->addStretch();buttons->addWidget(reloadButton);layout->addLayout(buttons);
        auto chosen=[&]()->int {int r=table->currentRow();return r>=0&&table->item(r,0)?table->item(r,0)->text().toInt():0;};
        connect(supplier,QOverload<int>::of(&QComboBox::currentIndexChanged),&dlg,[&](int){reload();});connect(status,QOverload<int>::of(&QComboBox::currentIndexChanged),&dlg,[&](int){reload();});connect(reloadButton,&QPushButton::clicked,&dlg,[&]{reload();});
        connect(pay,&QPushButton::clicked,&dlg,[&]{
            int id=chosen();if(!id)return;
            QSqlQuery read=query("SELECT supplier_id,original_cents-paid_cents FROM supplier_payables WHERE id=?",{id});if(!read.next()||read.value(1).toLongLong()<=0){QMessageBox::information(&dlg,"Ödeme","Açık borç yok.");return;}
            qint64 remaining=read.value(1).toLongLong();
            QDialog pd(&dlg);pd.setWindowTitle("Faturaya Bağlı Tedarikçi Ödemesi");auto *form=new QFormLayout(&pd);
            auto *amount=new QDoubleSpinBox;amount->setDecimals(2);amount->setRange(0.01,remaining/100.0);amount->setValue(remaining/100.0);amount->setSuffix(" TL");
            auto *note=new QLineEdit;note->setPlaceholderText("Dekont veya işlem açıklaması");form->addRow("Ödeme tutarı",amount);form->addRow("Açıklama",note);
            auto *box=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);form->addRow(box);connect(box,&QDialogButtonBox::accepted,&pd,&QDialog::accept);connect(box,&QDialogButtonBox::rejected,&pd,&QDialog::reject);if(pd.exec()!=QDialog::Accepted)return;
            const qint64 cents=qRound64(amount->value()*100);if(cents<=0)return;
            if(!db.transaction()){QMessageBox::warning(&dlg,"İşlem",db.lastError().text());return;}
            QSqlQuery current=query("SELECT supplier_id,original_cents-paid_cents FROM supplier_payables WHERE id=?",{id});
            bool ok=current.next()&&current.value(1).toLongLong()>=cents;const int supplierId=ok?current.value(0).toInt():0;
            if(ok)ok=run("UPDATE supplier_payables SET paid_cents=paid_cents+? WHERE id=? AND original_cents-paid_cents>=?",{cents,id,cents});
            if(ok){QSqlQuery c=query("SELECT changes()");ok=c.next()&&c.value(0).toInt()==1;}
            if(ok)ok=run("UPDATE parties SET balance_cents=balance_cents+? WHERE id=?",{cents,supplierId});
            if(ok)ok=run("INSERT INTO cash_moves(kind,amount_cents,description) VALUES('Tedarikçi Ödeme',?,?)",{-cents,QString("Borç #%1 - %2").arg(id).arg(note->text())});
            int moveId=0;if(ok){QSqlQuery last=query("SELECT last_insert_rowid()");ok=last.next();if(ok)moveId=last.value(0).toInt();}
            if(ok)ok=run("INSERT INTO supplier_payable_payments(payable_id,supplier_id,amount_cents,cash_move_id,note) VALUES(?,?,?,?,?)",{id,supplierId,cents,moveId,note->text()});
            if(ok)ok=run("INSERT INTO party_movements(party_id,kind,amount_cents,description) VALUES(?,'Tedarikçi Ödemesi',?,?)",{supplierId,cents,QString("Borç #%1: %2").arg(id).arg(note->text())});
            if(ok&&db.commit()){reload();refresh();QMessageBox::information(&dlg,"Ödeme","Ödeme faturaya işlendi.");}else{db.rollback();QMessageBox::warning(&dlg,"Ödeme","İşlem geri alındı.");}
        });
        connect(history,&QPushButton::clicked,&dlg,[&]{int id=chosen();if(!id)return;QDialog hd(&dlg);hd.setWindowTitle("Borç Ödeme Geçmişi");hd.resize(670,390);auto *v=new QVBoxLayout(&hd);auto *t=new QTableWidget;t->setColumnCount(3);t->setHorizontalHeaderLabels({"Tarih","Tutar","Açıklama"});t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);v->addWidget(t);QSqlQuery q=query("SELECT created_at,amount_cents,note FROM supplier_payable_payments WHERE payable_id=? ORDER BY id DESC",{id});while(q.next()){int r=t->rowCount();t->insertRow(r);t->setItem(r,0,new QTableWidgetItem(q.value(0).toString()));t->setItem(r,1,new QTableWidgetItem(money(q.value(1).toLongLong())));t->setItem(r,2,new QTableWidgetItem(q.value(2).toString()));}hd.exec();});
        connect(due,&QPushButton::clicked,&dlg,[&]{int id=chosen();if(!id)return;QSqlQuery q=query("SELECT due_date FROM supplier_payables WHERE id=?",{id});if(!q.next())return;QDialog dd(&dlg);dd.setWindowTitle("Vade Düzenle");auto *f=new QFormLayout(&dd);auto *picker=new QDateEdit;picker->setCalendarPopup(true);picker->setDisplayFormat("dd.MM.yyyy");picker->setDate(QDate::fromString(q.value(0).toString(),Qt::ISODate));f->addRow("Vade",picker);auto *b=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);f->addRow(b);connect(b,&QDialogButtonBox::accepted,&dd,&QDialog::accept);connect(b,&QDialogButtonBox::rejected,&dd,&QDialog::reject);if(dd.exec()==QDialog::Accepted&&run("UPDATE supplier_payables SET due_date=? WHERE id=?",{picker->date().toString(Qt::ISODate),id}))reload();});
        reload();dlg.exec();
    }
    void purchaseCenter(){
      QDialog d(this);d.setWindowTitle("Satın Alma Merkezi | Tedarikçi Siparişi ve Mal Kabul");d.resize(1060,690);
      auto *root=new QVBoxLayout(&d);
      auto *headline=new QLabel("SATIN ALMA  /  SİPARİŞ VE MAL KABUL");headline->setStyleSheet("font-size:21px;font-weight:850;color:#183047;padding:8px;");root->addWidget(headline);
      auto *top=new QHBoxLayout;auto *supplier=new QComboBox;auto *depot=new QComboBox;
      QSqlQuery suppliers=query("SELECT id,name FROM parties WHERE kind IN ('Tedarikci','Her ikisi') ORDER BY name");
      while(suppliers.next())supplier->addItem(suppliers.value(1).toString(),suppliers.value(0));
      QSqlQuery depots=query("SELECT id,name FROM warehouses ORDER BY name");
      while(depots.next())depot->addItem(depots.value(1).toString(),depots.value(0));
      top->addWidget(new QLabel("Tedarikçi"));top->addWidget(supplier,2);top->addWidget(new QLabel("Mal kabul deposu"));top->addWidget(depot,1);root->addLayout(top);
      auto *inputs=new QHBoxLayout;auto *products=new QComboBox;auto *quantity=new QSpinBox;quantity->setRange(1,1000000);auto *cost=new QDoubleSpinBox;cost->setRange(0.01,100000000);cost->setDecimals(2);cost->setSuffix(" TL");
      QSqlQuery pp=query("SELECT id,sku||' · '||name,cost_cents FROM products ORDER BY name");
      while(pp.next())products->addItem(pp.value(1).toString(),pp.value(0));
      QObject::connect(products,QOverload<int>::of(&QComboBox::currentIndexChanged),&d,[&](int){QSqlQuery q=query("SELECT cost_cents FROM products WHERE id=?",{products->currentData()});if(q.next())cost->setValue(q.value(0).toLongLong()/100.0);});
      if(products->count())products->setCurrentIndex(0);QSqlQuery initialCost=query("SELECT cost_cents FROM products WHERE id=?",{products->currentData()});if(initialCost.next())cost->setValue(initialCost.value(0).toLongLong()/100.0);
      auto *add=new QPushButton("+ Siparişe ekle");inputs->addWidget(products,4);inputs->addWidget(new QLabel("Miktar"));inputs->addWidget(quantity);inputs->addWidget(new QLabel("Birim alış"));inputs->addWidget(cost);inputs->addWidget(add);root->addLayout(inputs);
      auto *lines=new QTableWidget;lines->setColumnCount(4);lines->setHorizontalHeaderLabels({"Ürün","Miktar","Birim maliyet","Satır toplamı"});lines->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);lines->setSelectionBehavior(QAbstractItemView::SelectRows);lines->setEditTriggers(QAbstractItemView::NoEditTriggers);root->addWidget(lines,1);
      struct Line{int pid;QString name;int qty;qint64 unit;};QVector<Line> orderLines;
      auto *sum=new QLabel("Toplam: 0,00 TL");sum->setStyleSheet("color:#0b9489;font-size:20px;font-weight:800;");root->addWidget(sum);
      auto redraw=[&](){lines->setRowCount(0);qint64 total=0;for(const auto &line:orderLines){const int row=lines->rowCount();lines->insertRow(row);const qint64 lineTotal=line.qty*line.unit;total+=lineTotal;QStringList cells={line.name,QString::number(line.qty),money(line.unit),money(lineTotal)};for(int c=0;c<4;++c)lines->setItem(row,c,new QTableWidgetItem(cells[c]));}sum->setText("Toplam: "+money(total));};
      connect(add,&QPushButton::clicked,&d,[&]{if(products->currentIndex()<0)return;orderLines.append({products->currentData().toInt(),products->currentText(),quantity->value(),toCents(cost->value())});redraw();});
      auto *bottom=new QHBoxLayout;auto *remove=new QPushButton("Satırı kaldır");auto *save=new QPushButton("Siparişi oluştur");auto *receive=new QPushButton("Seçili siparişi mal kabul et");auto *close=new QPushButton("Kapat");
      bottom->addWidget(remove);bottom->addWidget(save);bottom->addWidget(receive);bottom->addStretch();bottom->addWidget(close);root->addLayout(bottom);
      connect(close,&QPushButton::clicked,&d,&QDialog::accept);
      auto *orderList=new QTableWidget;orderList->setColumnCount(6);orderList->setHorizontalHeaderLabels({"ID","Tedarikçi","Depo","Durum","Toplam","Tarih"});orderList->setSelectionBehavior(QAbstractItemView::SelectRows);orderList->setEditTriggers(QAbstractItemView::NoEditTriggers);orderList->horizontalHeader()->setStretchLastSection(true);orderList->setMaximumHeight(190);root->addWidget(new QLabel("SATIN ALMA SİPARİŞLERİ  •  Mal kabul için bekleyen bir sipariş seçin"));root->addWidget(orderList);
      auto reload=[&](){orderList->setRowCount(0);QSqlQuery q=query("SELECT o.id,p.name,w.name,o.status,o.total_cents,o.created_at FROM purchase_orders o JOIN parties p ON p.id=o.supplier_id JOIN warehouses w ON w.id=o.warehouse_id ORDER BY o.id DESC LIMIT 100");while(q.next()){int row=orderList->rowCount();orderList->insertRow(row);for(int c=0;c<6;++c){QString v=(c==4)?money(q.value(c).toLongLong()):q.value(c).toString();orderList->setItem(row,c,new QTableWidgetItem(v));}}};
      connect(remove,&QPushButton::clicked,&d,[&]{int row=lines->currentRow();if(row>=0&&row<orderLines.size()){orderLines.removeAt(row);redraw();}});
      connect(save,&QPushButton::clicked,&d,[&]{
        if(supplier->currentIndex()<0||depot->currentIndex()<0||orderLines.isEmpty()){QMessageBox::warning(&d,"Sipariş","Tedarikçi, depo ve en az bir ürün seçin.");return;}
        qint64 total=0;for(const auto &line:orderLines)total+=line.qty*line.unit;
        if(!db.transaction()){QMessageBox::warning(&d,"Veritabanı",db.lastError().text());return;}
        bool ok=run("INSERT INTO purchase_orders(supplier_id,warehouse_id,total_cents) VALUES(?,?,?)",{supplier->currentData(),depot->currentData(),total});
        int orderId=0;if(ok){QSqlQuery q=query("SELECT last_insert_rowid()");ok=q.next();if(ok)orderId=q.value(0).toInt();}
        for(const auto &line:orderLines){if(!ok)break;ok=run("INSERT INTO purchase_order_lines(order_id,product_id,qty,cost_cents) VALUES(?,?,?,?)",{orderId,line.pid,line.qty,line.unit});}
        if(ok&&db.commit()){orderLines.clear();redraw();reload();QMessageBox::information(&d,"Sipariş",QString("Satın alma siparişi #%1 oluşturuldu. Stok henüz artmadı.").arg(orderId));}
        else{db.rollback();QMessageBox::warning(&d,"Hata","Sipariş kaydedilemedi; işlem geri alındı.");}
      });
      connect(receive,&QPushButton::clicked,&d,[&]{
        const int row=orderList->currentRow();if(row<0||!orderList->item(row,0))return;
        const int orderId=orderList->item(row,0)->text().toInt();
        if(QMessageBox::question(&d,"Mal kabul",QString("#%1 numaralı siparişin tamamı teslim alındı mı? Stok ve tedarikçi borcu kaydedilecektir.").arg(orderId))!=QMessageBox::Yes)return;
        if(!db.transaction())return;
        QSqlQuery hdr=query("SELECT supplier_id,warehouse_id,total_cents FROM purchase_orders WHERE id=? AND status='Bekliyor'",{orderId});
        bool ok=hdr.next();int supplierId=0,warehouseId=0;qint64 total=0;int docId=0;
        if(ok){supplierId=hdr.value(0).toInt();warehouseId=hdr.value(1).toInt();total=hdr.value(2).toLongLong();}
        if(ok)ok=run("UPDATE purchase_orders SET status='Kabul Edildi',received_at=CURRENT_TIMESTAMP WHERE id=? AND status='Bekliyor'",{orderId});
        if(ok){QSqlQuery changes=query("SELECT changes()");ok=changes.next()&&changes.value(0).toInt()==1;}
        if(ok)ok=run("INSERT INTO docs(doc_type,status,party_id,total_cents) VALUES('Satin Alma','Tamamlandi',?,?)",{supplierId,total});
        if(ok){QSqlQuery q=query("SELECT last_insert_rowid()");ok=q.next();if(ok)docId=q.value(0).toInt();}
        QSqlQuery entries=query("SELECT product_id,qty,cost_cents FROM purchase_order_lines WHERE order_id=?",{orderId});
        int processed=0;
        while(ok&&entries.next()){
          const int pid=entries.value(0).toInt();const int qty=entries.value(1).toInt();const qint64 unit=entries.value(2).toLongLong();
          ok=run("INSERT OR IGNORE INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,?,0,0)",{pid,warehouseId})
            && run("UPDATE stock SET qty=qty+? WHERE product_id=? AND warehouse_id=?",{qty,pid,warehouseId})
            && run("INSERT INTO doc_lines(doc_id,product_id,warehouse_id,qty,unit_cents) VALUES(?,?,?,?,?)",{docId,pid,warehouseId,qty,unit})
            && run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason,doc_id) VALUES(?,?,?,?,?)",{pid,warehouseId,qty,"Satın alma mal kabul #"+QString::number(orderId),docId})
            && run("UPDATE products SET cost_cents=? WHERE id=?",{unit,pid});
          ++processed;
        }
        if(processed==0)ok=false;
        if(ok)ok=run("UPDATE parties SET balance_cents=balance_cents-? WHERE id=?",{total,supplierId})
          && run("INSERT INTO party_movements(party_id,kind,amount_cents,description) VALUES(?,'Alış borcu',?,?)",{supplierId,-total,"Mal kabul #"+QString::number(orderId)});
        if(ok && total>0)ok=run("INSERT INTO supplier_payables(supplier_id,purchase_order_id,doc_id,original_cents,due_date) VALUES(?,?,?,?,?)",{supplierId,orderId,docId,total,QDate::currentDate().addDays(30).toString(Qt::ISODate)});
        if(ok&&db.commit()){reload();refresh();QMessageBox::information(&d,"Mal kabul","Ürünler depoya aktarıldı; tedarikçi borcu kaydedildi.");}
        else{db.rollback();QMessageBox::warning(&d,"Mal kabul","Mal kabul başarısız oldu. Tüm değişiklikler geri alındı.");}
      });
      reload();d.exec();refresh();
    }
    void addRecord(){if(section=="Cari Kartlar")addParty();else if(section=="Ürün Yönetimi")addProduct();else if(section=="Depo / Stok")stockEntry();else if(section=="Kasa")addCash();else if(section=="Personel")addEmployee();}
    void chooseSection(const QString &name){section=name;if(dashboardCards)dashboardCards->setVisible(name=="Genel Bakış");heading->setText(name);filter->clear();primary->setVisible(name=="Cari Kartlar"||name=="Ürün Yönetimi"||name=="Depo / Stok"||name=="Kasa"||name=="Personel");primary->setText(name=="Ürün Yönetimi"?"+ Yeni ürün":name=="Depo / Stok"?"+ Stok işlemi":name=="Kasa"?"+ Kasa hareketi":name=="Personel"?"+ Personel":"+ Yeni cari");updateActions();refresh();}
public:
    void resizeEvent(QResizeEvent *event) override {
        QDialog::resizeEvent(event);
        if(actionGrid && actionsArea)updateActions();
    }
    explicit ManagementCenter(const QString &initial,QWidget *parent=nullptr):QDialog(parent){
        setWindowTitle("KasaPro Enterprise • Yönetim Merkezi V2.9");setMinimumSize(820,580);resize(1290,810);
        setStyleSheet(R"QSS(QDialog{background:#f4f6f8;color:#1d2938;font-family:'Segoe UI';font-size:13px} QListWidget#navigation{background:#17263c;border:0;color:#dfe8f4;font-size:14px;outline:none;} QListWidget#navigation::item{padding:16px 15px;margin:3px 10px;border-radius:10px;} QListWidget#navigation::item:selected{background:#ff592e;color:#fff;} QFrame#body{background:#ffffff;border-radius:16px;} QLabel#title{font-size:27px;font-weight:900;color:#1c2d40;} QLabel#stats{background:#eaf6f4;color:#087d75;padding:15px;border-radius:11px;font-weight:800;} QLineEdit{background:#fff;border:1px solid #d7dfe8;border-radius:10px;padding:12px;} QTableWidget{background:#fff;gridline-color:#f0f2f5;alternate-background-color:#fafcff;border:1px solid #e5eaf0;border-radius:10px;selection-background-color:#ffe2d9;selection-color:#17263c;} QHeaderView::section{background:#edf2f7;color:#54657b;padding:12px;border:0;font-weight:700;} QPushButton{padding:11px 17px;border-radius:10px;background:#fff;border:1px solid #e0e5eb;font-weight:700;color:#26374b;} QPushButton#primary{background:#ff592e;color:white;border:0;} QPushButton#toolbarAction{padding:8px 12px;min-height:39px;font-size:12px;} QScrollArea#managementToolbarScroll{background:transparent;border:none;} QWidget#managementToolbarPanel{background:transparent;} QPushButton:hover{border:1px solid #ff592e;})QSS");
        auto *root=new QHBoxLayout(this);root->setContentsMargins(0,0,0,0);root->setSpacing(0);auto *side=new QWidget;side->setFixedWidth(UiDesign::kSidebarWidth);side->setStyleSheet("background:#17263c;");auto *sv=new QVBoxLayout(side);auto *logo=new QLabel("KasaPro  <span style='color:#ff592e'>●</span>");logo->setStyleSheet("color:white;font-size:25px;font-weight:900;margin:19px 14px;");sv->addWidget(logo);nav=new QListWidget;nav->setObjectName("navigation");nav->addItems(modules);sv->addWidget(nav,1);auto *back=new QPushButton("← POS Ekranına Dön");sv->addWidget(back);connect(back,&QPushButton::clicked,this,&QDialog::accept);root->addWidget(side);
        auto *body=new QFrame;body->setObjectName("body");auto *v=new QVBoxLayout(body);v->setContentsMargins(18,16,18,18);v->setSpacing(16);auto *headerRow=new QHBoxLayout;
        auto *labels=new QVBoxLayout;
        labels->setSpacing(3);
        heading=new QLabel;
        heading->setObjectName("title");
        heading->setMinimumWidth(0);heading->setWordWrap(true);
        labels->addWidget(heading);
        subheading=new QLabel;
        subheading->setStyleSheet("color:#8a9aab;");
        labels->addWidget(subheading);
        headerRow->addLayout(labels,1);
        fullscreenButton=new QPushButton("⛶ Tam Ekran");fullscreenButton->setToolTip("F11 ile tam ekran değiştir");headerRow->addWidget(fullscreenButton,0,Qt::AlignTop);connect(fullscreenButton,&QPushButton::clicked,this,[this]{toggleFullscreen();});
        auto *f11=new QShortcut(QKeySequence(Qt::Key_F11),this);connect(f11,&QShortcut::activated,this,[this]{toggleFullscreen();});
        primary=new QPushButton;
        primary->setObjectName("primary");
        primary->setMinimumWidth(150);
        primary->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
        headerRow->addWidget(primary,0,Qt::AlignTop);
        connect(primary,&QPushButton::clicked,this,[this]{addRecord();});
        v->addLayout(headerRow);

        // Modüle özel tek satırlı araç çubuğu; tüm modüllerin tüm işlemleri gösterilmez.
        auto *toolbarScroll=new QScrollArea;
        actionsArea=toolbarScroll;
        toolbarScroll->setObjectName("managementToolbarScroll");
        toolbarScroll->setFrameShape(QFrame::NoFrame);
        toolbarScroll->setWidgetResizable(true);
        toolbarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        toolbarScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        toolbarScroll->setMinimumHeight(56);toolbarScroll->setMaximumHeight(230);
        auto *toolbarPanel=new QWidget;
        toolbarPanel->setObjectName("managementToolbarPanel");
        auto *actionsRow=new QGridLayout(toolbarPanel);
        actionGrid=actionsRow;
        int actionIndex=0;
        actionsRow->setContentsMargins(2,3,8,3);
        actionsRow->setSpacing(8);
        auto makeAction=[this,actionsRow,&actionIndex](const QString &label, auto callback){
            auto *button=new QPushButton(label);
            button->setObjectName("toolbarAction");
            UiDesign::polishAction(button);
            button->setMinimumWidth(0);
            button->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
            button->setToolTip(label);
            actionsRow->addWidget(button,actionIndex/4,actionIndex%4);
            ++actionIndex;
            moduleActions.insert(label,button);
            connect(button,&QPushButton::clicked,this,callback);
        };
        makeAction("+ Yeni Depo",[this]{addWarehouse();});
        makeAction("Ürünü Düzenle",[this]{editProduct();});
        makeAction("Depolar Arası Transfer",[this]{transferStock();});
        makeAction("Cari Tahsilat / Ödeme",[this]{cashParty();});
        makeAction("Cari Ekstre",[this]{showPartyHistory();});
        makeAction("Stok Sayımı",[this]{stockCountDialog();});
        makeAction("Sayım Geçmişi",[this]{stockCountHistory();});
        makeAction("Kasa Dökümü",[this]{cashAuditDialog();});
        makeAction("Satın Alma Merkezi",[this]{purchaseCenter();});
        makeAction("Sektör / Ürün Profilleri",[this]{SectorCatalogDialog dlg(this);dlg.exec();refresh();});
        makeAction("Kesirli Stok Defteri",[this]{FractionalStockDialog dlg(db,this);dlg.exec();refresh();});
        makeAction("Kesirli Pilot Alım / Satım",[this]{FractionalTradeDialog dlg(db,this);dlg.exec();refresh();});
        makeAction("Ambalaj / Koli Merkezi",[this]{PackagingCenter dlg(db,this);dlg.exec();refresh();});
        makeAction("Kapalı Koli / Açık Adet",[this]{CartonOpeningCenter dlg(db,this);dlg.exec();refresh();});
        makeAction("Kesirli Stok Mutabakatı",[this]{FractionalReconciliationDialog dlg(db,this);dlg.exec();});
        makeAction("Ürün Detayları",[this]{
            if(section!="Ürün Yönetimi")return;
            const int id=selectedId();
            if(id<=0){QMessageBox::information(this,"Ürün","Listeden düzenlenecek ürünü seçin.");return;}
            ProductDetailsDialog dlg(id,this);if(dlg.exec()==QDialog::Accepted)refresh();
        });
        makeAction("Varyant / Beden / Numara",[this]{
            if(section!="Ürün Yönetimi")return;
            const int id=selectedId();
            if(id<=0){QMessageBox::information(this,"Varyantlar","Önce bir ürün seçin.");return;}
            VariantManagerDialog dlg(db,id,this);dlg.exec();refresh();
        });
        makeAction("Türkçe Market Verisi",[this]{marketDemo();});
        makeAction("Listeyi Yenile",[this]{refresh();});
        makeAction("Tedarikçi Açık Borçları",[this]{supplierPayablesDialog();});
        toolbarScroll->setWidget(toolbarPanel);
        v->addWidget(toolbarScroll);
        dashboardCards=new QWidget;
        auto *cardsLayout=new QGridLayout(dashboardCards);
        cardsLayout->setContentsMargins(0,0,0,0);
        cardsLayout->setHorizontalSpacing(12);cardsLayout->setVerticalSpacing(12);
        const QStringList cardNames={"BUGÜNKÜ SATIŞ","KASA BAKİYESİ","ÜRÜN SAYISI","CARİ SAYISI","KRİTİK STOK SATIRI","BEKLEYEN ALIŞ"};
        for(int i=0;i<cardNames.size();++i){
            auto *card=new QFrame;card->setObjectName("kpiCard");
            card->setStyleSheet("QFrame#kpiCard{background:white;border:1px solid #e0e7ef;border-radius:12px;}");
            auto *box=new QVBoxLayout(card);box->setContentsMargins(15,11,15,11);box->setSpacing(5);
            auto *label=new QLabel(cardNames.at(i));label->setStyleSheet("color:#6a7a8c;font-size:11px;font-weight:700;");
            auto *value=new QLabel("—");value->setStyleSheet("color:#18334f;font-size:20px;font-weight:800;");
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            box->addWidget(label);box->addWidget(value);
            dashboardValues.append(value);
            cardsLayout->addWidget(card,i/3,i%3);
        }
        v->addWidget(dashboardCards);
        metrics=new QLabel;metrics->setObjectName("stats");metrics->setWordWrap(true);metrics->setMaximumHeight(75);v->addWidget(metrics);filter=new QLineEdit;filter->setPlaceholderText("Kayıtlarda ara: ürün, cari, kod veya açıklama...");v->addWidget(filter);listing=new QTableWidget;listing->setAlternatingRowColors(true);listing->setSelectionBehavior(QAbstractItemView::SelectRows);listing->setEditTriggers(QAbstractItemView::NoEditTriggers);v->addWidget(listing,1);root->addWidget(body,1);
        connect(filter,&QLineEdit::textChanged,this,[this]{refresh();});connect(nav,&QListWidget::currentTextChanged,this,[this](const QString &name){chooseSection(name);});
        int ix=modules.indexOf(initial);nav->setCurrentRow(ix>=0?ix:0);
    }
};

// EAN-8 ve EAN-13 kontrol basamagi; diger barkod bicimleri serbesttir.
static bool barcodeCheckDigitValid(const QString &value){
    if(value.size()!=8 && value.size()!=13)return true;
    for(const QChar &c:value)if(!c.isDigit())return true; // EAN disindaki kodlar icin zorunlu degil
    int sum=0;
    for(int i=value.size()-2,weight=3;i>=0;--i,weight=(weight==3?1:3))sum+=(value.at(i).unicode()-'0')*weight;
    return (10-(sum%10))%10 == value.back().unicode()-'0';
}


// Sale returns: recorded financial correction only for fully cash or fully credit sales.
// Card and mixed settlements are deliberately blocked until approved refund adapters exist.
static bool initSalesReturns(){
 return run("CREATE TABLE IF NOT EXISTS sale_returns(id INTEGER PRIMARY KEY,sale_doc_id INTEGER NOT NULL REFERENCES docs(id),party_id INTEGER,refund_method TEXT NOT NULL CHECK(refund_method IN ('Nakit','Veresiye')),refund_cents INTEGER NOT NULL CHECK(refund_cents>0),reason TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)")
 && run("CREATE TABLE IF NOT EXISTS sale_return_lines(id INTEGER PRIMARY KEY,return_id INTEGER NOT NULL REFERENCES sale_returns(id),sale_line_id INTEGER NOT NULL REFERENCES doc_lines(id),product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),qty INTEGER NOT NULL CHECK(qty>0),unit_cents INTEGER NOT NULL CHECK(unit_cents>=0))")
 && run("CREATE INDEX IF NOT EXISTS ix_return_lines_sale_line ON sale_return_lines(sale_line_id)");
}
static void salesReturnCenter(QWidget *parent){
 QDialog dialog(parent); dialog.setWindowTitle("KasaPro | Satış İadeleri");dialog.resize(1030,740);
 auto *layout=new QVBoxLayout(&dialog);
 auto *title=new QLabel("Satış İadesi ve Satış Geçmişi");title->setStyleSheet("font-size:23px;font-weight:800;color:#1e384e");layout->addWidget(title);
 auto *help=new QLabel("Yalnızca tamamı nakit veya tamamı veresiye satışlarda muhasebeli iade yapılır. Kart/karma ödemeler için resmi cihaz/banka iadesi gerekir.");help->setWordWrap(true);layout->addWidget(help);
 auto *filter=new QLineEdit;filter->setPlaceholderText("Satış belge numarası veya müşteri adı...");layout->addWidget(filter);
 auto *sales=new QTableWidget;sales->setColumnCount(5);sales->setHorizontalHeaderLabels({"Belge", "Tarih", "Müşteri", "Tutar", "Ödeme"});sales->setSelectionBehavior(QAbstractItemView::SelectRows);sales->setSelectionMode(QAbstractItemView::SingleSelection);sales->setEditTriggers(QAbstractItemView::NoEditTriggers);sales->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(sales,2);
 auto *lines=new QTableWidget;lines->setColumnCount(6);lines->setHorizontalHeaderLabels({"Satır ID","Ürün","Depo","Satılan","İade edilmiş","Birim fiyat"});lines->setSelectionBehavior(QAbstractItemView::SelectRows);lines->setSelectionMode(QAbstractItemView::SingleSelection);lines->setEditTriggers(QAbstractItemView::NoEditTriggers);lines->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);lines->hideColumn(0);layout->addWidget(lines,2);
 auto *actions=new QHBoxLayout;auto *qty=new QSpinBox;qty->setRange(1,1000000);auto *reason=new QLineEdit;reason->setPlaceholderText("İade gerekçesi (zorunlu)");auto *refund=new QPushButton("Seçili Satırı İade Et");refund->setStyleSheet("background:#ef572d;color:white;font-weight:bold;padding:12px;border-radius:8px");auto *receipt=new QPushButton("Satış Bilgi Fişi Kaydet");auto *printReceipt=new QPushButton("58/80 mm Fiş Yazdır");actions->addWidget(new QLabel("İade adedi"));actions->addWidget(qty);actions->addWidget(reason,1);actions->addWidget(refund);layout->addLayout(actions);layout->addWidget(receipt);layout->addWidget(printReceipt);
 auto selectedSale=[&]()->int{auto *item=sales->item(sales->currentRow(),0);return item?item->text().toInt():0;};
 auto reloadSales=[&](){sales->setRowCount(0);QSqlQuery q=query("SELECT d.id,d.created_at,COALESCE(p.name,'Perakende'),d.total_cents,COALESCE(GROUP_CONCAT(pay.method||':'||pay.amount_cents),'-') FROM docs d LEFT JOIN parties p ON p.id=d.party_id LEFT JOIN pos_payments pay ON pay.doc_id=d.id WHERE d.doc_type='Satis' AND d.status='Tamamlandi' AND (CAST(d.id AS TEXT) LIKE ? OR COALESCE(p.name,'') LIKE ?) GROUP BY d.id ORDER BY d.id DESC LIMIT 250",{"%"+filter->text().trimmed()+"%","%"+filter->text().trimmed()+"%"});while(q.next()){int r=sales->rowCount();sales->insertRow(r);QStringList values={q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),money(q.value(3).toLongLong()),q.value(4).toString()};for(int i=0;i<values.size();++i)sales->setItem(r,i,new QTableWidgetItem(values.at(i)));}};
 auto reloadLines=[&](){lines->setRowCount(0);int sale=selectedSale();if(sale<1)return;QSqlQuery q=query("SELECT l.id,p.name,w.name,l.qty,COALESCE((SELECT SUM(rl.qty) FROM sale_return_lines rl JOIN sale_returns sr ON sr.id=rl.return_id WHERE rl.sale_line_id=l.id),0),l.unit_cents FROM doc_lines l JOIN products p ON p.id=l.product_id JOIN warehouses w ON w.id=l.warehouse_id WHERE l.doc_id=?",{sale});while(q.next()){int r=lines->rowCount();lines->insertRow(r);for(int c=0;c<6;c++){auto *item=new QTableWidgetItem(c==5?money(q.value(c).toLongLong()):q.value(c).toString());lines->setItem(r,c,item);}}};
 QObject::connect(filter,&QLineEdit::textChanged,&dialog,[&](const QString&){reloadSales();reloadLines();});
 QObject::connect(sales,&QTableWidget::itemSelectionChanged,&dialog,[&](){reloadLines();});
 QObject::connect(refund,&QPushButton::clicked,&dialog,[&](){
   const int saleId=selectedSale();const int selectedRow=lines->currentRow();if(saleId<1||selectedRow<0||!lines->item(selectedRow,0)){QMessageBox::warning(&dialog,"İade","Satış ve ürün satırı seçiniz.");return;}
   const int lineId=lines->item(selectedRow,0)->text().toInt();const int count=qty->value();const QString explanation=reason->text().trimmed();if(explanation.size()<3){QMessageBox::warning(&dialog,"İade","En az 3 karakterli gerekçe yazınız.");return;}
   // Defensive guard: if the sale has card payment, mixed methods, or old unrecorded payments, stop.
   QSqlQuery mode=query("SELECT method,COUNT(*),SUM(amount_cents) FROM pos_payments WHERE doc_id=? GROUP BY method",{saleId});QString payMethod;int methods=0;qint64 paid=0;while(mode.next()){payMethod=mode.value(0).toString();paid=mode.value(2).toLongLong();++methods;}
   QSqlQuery origin=query("SELECT total_cents,party_id FROM docs WHERE id=? AND doc_type='Satis' AND status='Tamamlandi'",{saleId});if(!origin.next())return;const qint64 saleTotal=origin.value(0).toLongLong();const int partyId=origin.value(1).toInt();
   if(methods!=1 || (payMethod!="Nakit"&&payMethod!="Veresiye") || paid!=saleTotal){QMessageBox::warning(&dialog,"Kart/karma ödeme","Bu satışın ödeme yöntemi kart, karma veya doğrulanamayan geçmiş kayıttır. Gerçek cihaz/banka iadesi olmadan otomatik muhasebe iadesi yapılamaz.");return;}
   if(payMethod=="Veresiye" && partyId<=0){QMessageBox::warning(&dialog,"Cari","Satışın cari kartı eksik.");return;}
   if(!SupervisorGate::authorize(&dialog,db,QString("Satış #%1, satır #%2, %3 adet iadesi").arg(saleId).arg(lineId).arg(count),saleId))return;
   if(!db.transaction()){QMessageBox::warning(&dialog,"Veritabanı",db.lastError().text());return;}
   bool ok=true;int productId=0,warehouseId=0;int sold=0;int previouslyReturned=0;qint64 unit=0;
   QSqlQuery detail=query("SELECT l.product_id,l.warehouse_id,l.qty,l.unit_cents,COALESCE((SELECT SUM(rl.qty) FROM sale_return_lines rl WHERE rl.sale_line_id=l.id),0) FROM doc_lines l WHERE l.id=? AND l.doc_id=?",{lineId,saleId});if(!detail.next())ok=false;else{productId=detail.value(0).toInt();warehouseId=detail.value(1).toInt();sold=detail.value(2).toInt();unit=detail.value(3).toLongLong();previouslyReturned=detail.value(4).toInt();}
   if(count<=0||count>sold-previouslyReturned)ok=false;
   const qint64 amount=unit*count;if(amount<=0)ok=false;
   if(ok)ok=run("INSERT INTO sale_returns(sale_doc_id,party_id,refund_method,refund_cents,reason) VALUES(?,?,?,?,?)",{saleId,partyId>0?QVariant(partyId):QVariant(),payMethod,amount,explanation});
   int returnId=0;if(ok){auto id=query("SELECT last_insert_rowid()");ok=id.next();if(ok)returnId=id.value(0).toInt();}
   if(ok)ok=run("INSERT INTO sale_return_lines(return_id,sale_line_id,product_id,warehouse_id,qty,unit_cents) VALUES(?,?,?,?,?,?)",{returnId,lineId,productId,warehouseId,count,unit});
   if(ok)ok=run("INSERT OR IGNORE INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,?,0,0)",{productId,warehouseId});
   if(ok)ok=run("UPDATE stock SET qty=qty+? WHERE product_id=? AND warehouse_id=?",{count,productId,warehouseId});
   if(ok)ok=run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason,doc_id) VALUES(?,?,?,'Satis Iadesi',?)",{productId,warehouseId,count,saleId});
   if(ok&&payMethod=="Nakit")ok=run("INSERT INTO cash_moves(kind,amount_cents,description,doc_id) VALUES('Iade',? ,?,?)",{-amount,QString("Satış #%1, iade #%2: %3").arg(saleId).arg(returnId).arg(explanation),saleId});
   if(ok&&payMethod=="Veresiye")ok=run("UPDATE parties SET balance_cents=balance_cents-? WHERE id=?",{amount,partyId});
   if(ok&&payMethod=="Veresiye")ok=run("INSERT INTO party_movements(party_id,kind,amount_cents,description) VALUES(?,'Satis Iadesi',?,?)",{partyId,-amount,QString("Satış #%1, iade #%2: %3").arg(saleId).arg(returnId).arg(explanation)});
   if(ok)ok=db.commit();if(!ok){db.rollback();QMessageBox::warning(&dialog,"İade hatası","İşlem kaydedilemedi. Veritabanındaki değişiklikler geri alındı.");return;}
   QMessageBox::information(&dialog,"İade tamamlandı",QString("İade #%1: %2\nStok geri alındı; %3 hareketi kaydedildi.\nBu kayıt resmi ÖKC/banka iade belgesi yerine geçmez.").arg(returnId).arg(money(amount),payMethod));reloadLines();reloadSales();
 });
 QObject::connect(printReceipt,&QPushButton::clicked,&dialog,[&](){
   const int saleId=selectedSale();if(saleId<1){QMessageBox::information(&dialog,"Fiş","Önce satış seçiniz.");return;}
   QDialog settings(&dialog);settings.setWindowTitle("Bilgi fişi • Yazıcı ve kağıt");auto *form=new QFormLayout(&settings);
   auto *paper=new QComboBox;paper->addItem("58 mm",58);paper->addItem("80 mm",80);
   auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
   form->addRow("Kağıt genişliği",paper);form->addRow(buttons);
   QObject::connect(buttons,&QDialogButtonBox::accepted,&settings,&QDialog::accept);
   QObject::connect(buttons,&QDialogButtonBox::rejected,&settings,&QDialog::reject);
   if(settings.exec()!=QDialog::Accepted)return;
   const int width=paper->currentData().toInt();
   auto sale=query("SELECT d.created_at,COALESCE(p.name,'Perakende müşteri'),d.total_cents FROM docs d LEFT JOIN parties p ON p.id=d.party_id WHERE d.id=? AND d.doc_type='Satis'",{saleId});
   if(!sale.next()){QMessageBox::warning(&dialog,"Fiş","Satış bulunamadı.");return;}
   QString html=QString("<html><body style='font-family:monospace;font-size:9pt'><center><b>KASAPRO</b><br>MALİ DEĞERİ OLMAYAN BİLGİ FİŞİ<br>Satış #%1<br>%2</center><hr>Müşteri: %3<hr><table width='100%'>")
     .arg(saleId).arg(sale.value(0).toString().toHtmlEscaped()).arg(sale.value(1).toString().toHtmlEscaped());
   auto items=query("SELECT p.name,l.qty,l.unit_cents FROM doc_lines l JOIN products p ON p.id=l.product_id WHERE l.doc_id=?",{saleId});
   while(items.next()){
      const int qty=items.value(1).toInt();const qint64 unit=items.value(2).toLongLong();
      html+=QString("<tr><td>%1<br><small>%2 x %3</small></td><td align='right'>%4</td></tr>")
       .arg(items.value(0).toString().toHtmlEscaped()).arg(qty).arg(money(unit)).arg(money(unit*qty));
   }
   html+=QString("</table><hr><b>TOPLAM: %1</b><hr><center>Bu belge resmi ÖKC fişi veya e-belge değildir.</center></body></html>").arg(money(sale.value(2).toLongLong()));
   QPrinter printer(QPrinter::HighResolution);
   printer.setPageSize(QPageSize(QSizeF(width,220),QPageSize::Millimeter,"KasaPro Rulo"));
   printer.setPageMargins(QMarginsF(2,3,2,3),QPageLayout::Millimeter);
   QPrintDialog printDialog(&printer,&dialog);printDialog.setWindowTitle("Bilgi fişini yazdır");
   if(printDialog.exec()!=QDialog::Accepted)return;
   QTextDocument document;document.setHtml(html);document.print(&printer);
 });
 QObject::connect(receipt,&QPushButton::clicked,&dialog,[&](){int id=selectedSale();if(id<1)return;QString path=QFileDialog::getSaveFileName(&dialog,"Satış bilgi çıktısı",QString("Satis_%1.txt").arg(id),"Metin dosyası (*.txt)");if(path.isEmpty())return;QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(&dialog,"Fiş","Dosya kaydedilemedi.");return;}QTextStream out(&file);out<<"KASAPRO - MALİ DEĞERİ OLMAYAN BİLGİ FİŞİ\n"<<"Satış #"<<id<<"\n";QSqlQuery q=query("SELECT p.name,l.qty,l.unit_cents FROM doc_lines l JOIN products p ON p.id=l.product_id WHERE l.doc_id=?",{id});while(q.next())out<<q.value(0).toString()<<" x"<<q.value(1).toInt()<<"  "<<money(q.value(1).toLongLong()*q.value(2).toLongLong())<<"\n";out<<"Resmi ÖKC fişi veya e-belge değildir.\n";QMessageBox::information(&dialog,"Bilgi fişi","Metin çıktısı kaydedildi.");});
 reloadSales();dialog.exec();
}

class PosWindow final:public QMainWindow {
    struct BasketLine {int id=0; int qty=0; int packId=0; int packUnits=1; qint64 price=0; QString title; QString image;};
    QVector<BasketLine> basket;
    int depotId=1;
    int customerId=0;
    int currentCategory=-1;
    QComboBox *depotPicker=nullptr;
    QLineEdit *search=nullptr;
    QLineEdit *barcodeInput=nullptr;
    QLabel *barcodeState=nullptr;
    QWidget *tiles=nullptr;
    QGridLayout *tileGrid=nullptr;
    QScrollArea *productScrollArea=nullptr;
    int visibleGridColumns=4;
    QVBoxLayout *cartRows=nullptr;
    QWidget *cartHolder=nullptr;
    QLabel *subtotal=nullptr,*grand=nullptr,*customerText=nullptr,*clockLabel=nullptr,*countLabel=nullptr;
    QHBoxLayout *categoryBar=nullptr;

    void alert(const QString &v){QMessageBox::warning(this,"KasaPro POS",v);}
    qint64 cartTotal()const{qint64 n=0;for(const BasketLine &b:basket)n+=b.price*b.qty;return n;}
    void wipe(QLayout *layout){while(QLayoutItem *child=layout->takeAt(0)){if(child->widget())delete child->widget();if(child->layout())wipe(child->layout());delete child;}}
    QPixmap picture(const QString &path,int w,int h){QPixmap image(path);return image.isNull()?QPixmap():image.scaled(w,h,Qt::KeepAspectRatio,Qt::SmoothTransformation);}
    void refreshDepot(){int old=depotId;depotPicker->blockSignals(true);depotPicker->clear();auto q=query("SELECT id,name FROM warehouses ORDER BY name");while(q.next())depotPicker->addItem(q.value(1).toString(),q.value(0));int idx=depotPicker->findData(old);if(idx>=0)depotPicker->setCurrentIndex(idx);depotId=depotPicker->currentData().toInt();depotPicker->blockSignals(false);}
    int available(int id){auto q=query("SELECT COALESCE(qty-reserved,0) FROM stock WHERE product_id=? AND warehouse_id=?",{id,depotId});return q.next()?q.value(0).toInt():0;}
    void refreshCart(){wipe(cartRows);for(const BasketLine &item:basket){auto *line=new QFrame;line->setStyleSheet("QFrame{background:#fff;border-bottom:1px solid #e7ecef;} QLabel{border:0;background:transparent;}");auto *layout=new QHBoxLayout(line);layout->setContentsMargins(4,7,4,7);
      auto *photo=new QLabel;photo->setFixedSize(45,48);photo->setAlignment(Qt::AlignCenter);auto pm=picture(item.image,43,43);if(pm.isNull())photo->setText("📦");else photo->setPixmap(pm);layout->addWidget(photo);
      auto *name=new QLabel(item.title);name->setWordWrap(true);name->setMinimumWidth(85);layout->addWidget(name,1);
      auto *less=new QPushButton("−");less->setFixedSize(30,30);less->setStyleSheet(orangeButton());layout->addWidget(less);
      auto *qty=new QLabel(QString::number(item.qty));qty->setAlignment(Qt::AlignCenter);qty->setFixedWidth(23);layout->addWidget(qty);
      auto *more=new QPushButton("+");more->setFixedSize(30,30);more->setStyleSheet(orangeButton());layout->addWidget(more);
      auto *price=new QLabel(money(item.price*item.qty));price->setAlignment(Qt::AlignRight|Qt::AlignVCenter);price->setMinimumWidth(88);layout->addWidget(price);
      connect(less,&QPushButton::clicked,this,[this,id=item.id,packId=item.packId,packUnits=item.packUnits,price=item.price]{for(int i=0;i<basket.size();++i){if(basket[i].id==id && basket[i].packId==packId && basket[i].price==price){const int decrement=packId>0?qMax(1,packUnits):1;if((basket[i].qty-=decrement)<=0)basket.removeAt(i);break;}}refreshCart();});
      connect(more,&QPushButton::clicked,this,[this,id=item.id,packId=item.packId]{if(packId>0)addPackToCart(id,packId);else addToCart(id);});cartRows->addWidget(line);
    }cartRows->addStretch();subtotal->setText(money(cartTotal()));grand->setText(money(cartTotal()));countLabel->setText(QString("%1 ürün · %2 adet").arg(basket.size()).arg([this](){int n=0;for(auto &b:basket)n+=b.qty;return n;}()));}
    bool addToCart(int id,int units=1){
      if(units<=0||units>10000){alert("Geçersiz paket miktarı.");return false;}
      qint64 held=0;for(const BasketLine &b:basket)if(b.id==id)held+=b.qty;
      const qint64 freeStock=available(id);
      if(held+units>freeStock){alert(QString("Yetersiz stok. Sepette %1, eklemek istenen %2, kullanılabilir %3.").arg(held).arg(units).arg(freeStock));return false;}
      for(BasketLine &b:basket)if(b.id==id && b.packId==0){b.qty+=units;refreshCart();return true;}
      QSqlQuery q=query("SELECT name,price_cents,image_path FROM products WHERE id=?",{id});if(!q.next())return false;
      BasketLine b;b.id=id;b.qty=units;b.title=q.value(0).toString();b.price=q.value(1).toLongLong();b.image=q.value(2).toString();basket.append(b);refreshCart();return true;
    }
    // V4.1: Pack barcode sales use exact integer cents per base unit.
    // Pack prices that cannot be represented by the existing doc_lines schema are explicitly rejected.
    bool addPackToCart(int productId,int packId,int packCount=1){
      QSqlQuery pack=query("SELECT label,units_per_pack,sale_price_cents FROM product_packs WHERE id=? AND product_id=?",{packId,productId});
      if(!pack.next()){alert("Ambalaj tanımı bulunamadı.");return false;}
      const QString label=pack.value(0).toString();
      const qint64 units=pack.value(1).toLongLong(),packPrice=pack.value(2).toLongLong();
      const qint64 baseQty=units*packCount;
      if(units<=0||packCount<=0||baseQty>10000){alert("Ambalaj miktarı satış sınırını aşıyor.");return false;}
      if(packPrice>0 && packPrice%units!=0){
        alert(QString("%1 ambalaj fiyatı (%2), %3 ana adede kuruş kaybı olmadan bölünemiyor. Fiyatı düzenleyin; yuvarlanmış satış yapılmadı.")
              .arg(label,money(packPrice)).arg(units));return false;
      }
      QSqlQuery product=query("SELECT price_cents FROM products WHERE id=?",{productId});
      if(!product.next())return false;
      const qint64 unitPrice=packPrice>0?packPrice/units:product.value(0).toLongLong();
      // Same item at two different prices must remain separate basket lines.
      qint64 already=0;for(const BasketLine &b:basket)if(b.id==productId)already+=b.qty;
      if(already+baseQty>available(productId)){alert("Ambalaj için yeterli kullanılabilir stok yok.");return false;}
      QSqlQuery prod=query("SELECT name,image_path FROM products WHERE id=?",{productId});if(!prod.next())return false;
      for(BasketLine &b:basket){if(b.id==productId && b.price==unitPrice && b.packId==packId && b.packUnits==units){b.qty+=int(baseQty);refreshCart();return true;}}
      BasketLine item;item.id=productId;item.qty=int(baseQty);item.price=unitPrice;item.packId=packId;item.packUnits=int(units);
      item.title=prod.value(0).toString()+" · "+label;
      item.image=prod.value(1).toString();basket.append(item);refreshCart();return true;
    }
    void choosePackForSale(){
      QDialog dlg(this);dlg.setWindowTitle("Koli / Paket / Adet Satışı");dlg.resize(650,340);
      auto *layout=new QFormLayout(&dlg);
      auto *searchPack=new QLineEdit;searchPack->setPlaceholderText("Ürün adı / stok kodu / barkod");layout->addRow("Ürün bul",searchPack);
      auto *packSelect=new QComboBox;layout->addRow("Satılacak ambalaj",packSelect);
      auto *packCount=new QSpinBox;packCount->setRange(1,1000);layout->addRow("Ambalaj sayısı",packCount);
      auto *status=new QLabel("Ürün veya ambalaj barkodu arayın.");status->setWordWrap(true);layout->addRow(status);
      auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addRow(buttons);
      auto refresh=[&](){packSelect->clear();const QString term="%"+searchPack->text().trimmed()+"%";
        QSqlQuery q=query("SELECT pk.id,pk.product_id,p.name,pk.label,pk.units_per_pack,pk.sale_price_cents "
          "FROM product_packs pk JOIN products p ON p.id=pk.product_id "
          "WHERE p.name LIKE ? OR p.sku LIKE ? OR pk.barcode LIKE ? "
          "ORDER BY p.name,pk.units_per_pack LIMIT 100",{term,term,term});
        while(q.next())packSelect->addItem(QString("%1 — %2 × %3 (%4)").arg(q.value(2).toString(),q.value(3).toString(),q.value(4).toString(),q.value(5).toLongLong()>0?money(q.value(5).toLongLong()):"Ürün liste fiyatı"),
          QVariantList{q.value(1),q.value(0)});
        status->setText(QString("%1 ambalaj tanımı bulundu. Satış depo stokundan yapılır.").arg(packSelect->count()));};
      connect(searchPack,&QLineEdit::textChanged,&dlg,[&](const QString &){refresh();});
      connect(buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
      connect(buttons,&QDialogButtonBox::accepted,&dlg,[&](){const QVariantList data=packSelect->currentData().toList();
        if(data.size()!=2)return;if(addPackToCart(data.at(0).toInt(),data.at(1).toInt(),packCount->value()))dlg.accept();});
      refresh();dlg.exec();
    }
    // Parent products represent a style; each child represents an independently stocked option.
    // No stock is moved here: selected child id enters the existing cart and checkout pipeline.
    int availableVariantCount(int parentId){
      auto q=query("SELECT COUNT(*) FROM product_variants v JOIN stock s ON s.product_id=v.child_product_id AND s.warehouse_id=? WHERE v.parent_product_id=? AND s.qty>s.reserved",{depotId,parentId});
      return q.next()?q.value(0).toInt():0;
    }
    bool hasVariants(int productId){
      auto q=query("SELECT 1 FROM product_variants WHERE parent_product_id=? LIMIT 1",{productId});
      return q.next();
    }
    void chooseVariantForSale(int parentId){
      QDialog dialog(this);dialog.setWindowTitle("Ürün seçenekleri | Beden / Numara / Renk");dialog.resize(790,565);
      auto *layout=new QVBoxLayout(&dialog);
      QSqlQuery parent=query("SELECT name FROM products WHERE id=?",{parentId});
      const QString parentName=parent.next()?parent.value(0).toString():QString("Ürün #%1").arg(parentId);
      auto *title=new QLabel(parentName);title->setWordWrap(true);title->setStyleSheet("font-size:18px;font-weight:800;");layout->addWidget(title);
      auto *hint=new QLabel("Mevcut depodaki kullanılabilir stok gösterilir. Stok yoksa seçenek sepete eklenemez.");hint->setWordWrap(true);layout->addWidget(hint);
      auto *filter=new QLineEdit;filter->setPlaceholderText("Beden, numara, renk veya stok koduyla filtrele...");layout->addWidget(filter);
      auto *table=new QTableWidget;table->setColumnCount(6);
      table->setHorizontalHeaderLabels({"Beden / Numara","Renk / Seçenek","Stok Kodu","Barkod","Fiyat","Kullanılabilir"});
      table->setSelectionBehavior(QAbstractItemView::SelectRows);
      table->setSelectionMode(QAbstractItemView::SingleSelection);
      table->setEditTriggers(QAbstractItemView::NoEditTriggers);
      table->verticalHeader()->hide();table->verticalHeader()->setDefaultSectionSize(42);
      table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
      table->horizontalHeader()->setStretchLastSection(true);
      layout->addWidget(table,1);
      auto *bottom=new QHBoxLayout;
      auto *count=new QLabel;bottom->addWidget(count);bottom->addStretch();
      auto *cancel=new QPushButton("Vazgeç");auto *add=new QPushButton("Seçili Varyantı Sepete Ekle");
      add->setEnabled(false);bottom->addWidget(cancel);bottom->addWidget(add);layout->addLayout(bottom);
      connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
      auto reload=[&](){
        table->setRowCount(0);const QString t=filter->text().trimmed();
        QSqlQuery q=query("SELECT p.id,v.option1,v.option2,p.sku,p.barcode,p.price_cents,COALESCE(s.qty-s.reserved,0) "
           "FROM product_variants v JOIN products p ON p.id=v.child_product_id "
           "LEFT JOIN stock s ON s.product_id=p.id AND s.warehouse_id=? "
           "WHERE v.parent_product_id=? AND (v.option1 LIKE ? OR v.option2 LIKE ? OR p.sku LIKE ?) "
           "ORDER BY v.option1,v.option2",{depotId,parentId,"%"+t+"%","%"+t+"%","%"+t+"%"});
        int availableRows=0;
        while(q.next()){
          const int row=table->rowCount();table->insertRow(row);
          const int itemId=q.value(0).toInt();const int free=q.value(6).toInt();
          const QStringList values={q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),
             q.value(4).toString(),money(q.value(5).toLongLong()),QString::number(free)};
          for(int col=0;col<6;++col){auto *cell=new QTableWidgetItem(values[col]);
            if(free<=0)cell->setForeground(QColor("#9CA3AF"));table->setItem(row,col,cell);}
          table->item(row,0)->setData(Qt::UserRole,itemId);
          table->item(row,0)->setData(Qt::UserRole+1,free);
          if(free>0)++availableRows;
        }
        count->setText(QString("%1 seçenek • %2 stoklu").arg(table->rowCount()).arg(availableRows));
        add->setEnabled(false);
      };
      connect(filter,&QLineEdit::textChanged,&dialog,[&](const QString&){reload();});
      connect(table,&QTableWidget::itemSelectionChanged,&dialog,[&]{
        const int row=table->currentRow();
        add->setEnabled(row>=0 && table->item(row,0) && table->item(row,0)->data(Qt::UserRole+1).toInt()>0);
      });
      connect(add,&QPushButton::clicked,&dialog,[&]{
        const int row=table->currentRow();if(row<0||!table->item(row,0))return;
        const int id=table->item(row,0)->data(Qt::UserRole).toInt();
        if(addToCart(id))dialog.accept();
        else reload();
      });
      connect(table,&QTableWidget::cellDoubleClicked,&dialog,[&](int,int){if(add->isEnabled())add->click();});
      reload();dialog.exec();
    }
    void barcodeManager(){
      QDialog dialog(this);dialog.setWindowTitle("Barkod Merkezi | Çoklu Barkod");dialog.resize(850,610);
      auto *root=new QVBoxLayout(&dialog);
      auto *caption=new QLabel("Ürüne alternatif barkod ekle • Mevcut stok kodu ve ana barkod korunur");
      caption->setWordWrap(true);root->addWidget(caption);
      auto *selector=new QComboBox;selector->setMinimumHeight(41);root->addWidget(selector);
      auto products=query("SELECT id,sku||'  •  '||name FROM products ORDER BY name");
      while(products.next())selector->addItem(products.value(1).toString(),products.value(0));
      auto *table=new QTableWidget;table->setColumnCount(4);table->setHorizontalHeaderLabels({"ID","Alternatif Barkod","Açıklama","Paket Adedi"});
      table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setEditTriggers(QAbstractItemView::NoEditTriggers);
      table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
      table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
      table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
      root->addWidget(table,1);
      auto refresh=[&](){table->setRowCount(0);auto q=query("SELECT id,barcode,description,unit_multiplier FROM product_barcodes WHERE product_id=? ORDER BY id DESC",{selector->currentData()});
        while(q.next()){int row=table->rowCount();table->insertRow(row);for(int col=0;col<4;col++)table->setItem(row,col,new QTableWidgetItem(q.value(col).toString()));}};
      connect(selector,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){refresh();});
      auto *form=new QHBoxLayout;auto *code=new QLineEdit;code->setMaxLength(64);code->setPlaceholderText("EAN-8 / EAN-13 / Code128 / ürün kodu");
      auto *note=new QLineEdit;note->setPlaceholderText("Açıklama (örn. 24'lü koli)");
      auto *multiplier=new QSpinBox;multiplier->setRange(1,10000);multiplier->setValue(1);multiplier->setPrefix("Adet: ");multiplier->setToolTip("Bu barkod okutulunca sepete eklenecek tekil stok adedi. 1 = normal ürün, 24 = 24'lü koli.");
      auto *add=new QPushButton("+ Barkod Ekle");form->addWidget(code,2);form->addWidget(note,2);form->addWidget(multiplier);form->addWidget(add);root->addLayout(form);
      auto *row=new QHBoxLayout;auto *remove=new QPushButton("Seçili Barkodu Sil");auto *labelPrint=new QPushButton("Etiket Yazdır / PDF");auto *close=new QPushButton("Kapat");row->addWidget(remove);row->addWidget(labelPrint);row->addStretch();row->addWidget(close);root->addLayout(row);
      connect(labelPrint,&QPushButton::clicked,&dialog,[&]{
        if(selector->currentIndex()<0)return;
        QString labelCode;
        const int selected=table->currentRow();
        if(selected>=0 && table->item(selected,1))labelCode=table->item(selected,1)->text();
        if(labelCode.isEmpty()){
          auto product=query("SELECT COALESCE(NULLIF(barcode,''),sku) FROM products WHERE id=?",{selector->currentData()});
          if(product.next())labelCode=product.value(0).toString();
        }
        auto productName=query("SELECT name FROM products WHERE id=?",{selector->currentData()});
        const QString name=productName.next()?productName.value(0).toString():selector->currentText();
        BarcodeLabelPrinter::show(&dialog,name,labelCode);
      });
      connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
      connect(add,&QPushButton::clicked,&dialog,[&]{
        const QString value=code->text().trimmed();
        if(selector->currentIndex()<0 || value.isEmpty()){QMessageBox::warning(&dialog,"Barkod","Ürün ve barkod seçiniz.");return;}
        if(!barcodeCheckDigitValid(value)){QMessageBox::warning(&dialog,"Geçersiz EAN","EAN-8 / EAN-13 kontrol basamağı hatalı.");return;}
        auto duplicate=query("SELECT id FROM products WHERE barcode=? OR sku=? LIMIT 1",{value,value});
        if(duplicate.next()){QMessageBox::warning(&dialog,"Çakışma","Bu kod başka bir ürünün ana barkodu veya stok kodu olarak kayıtlı.");return;}
        QString error;
        if(!run("INSERT INTO product_barcodes(product_id,barcode,description,unit_multiplier) VALUES(?,?,?,?)",{selector->currentData(),value,note->text().trimmed(),multiplier->value()},&error)){
          QMessageBox::warning(&dialog,"Barkod kaydedilemedi",error);return;
        }code->clear();note->clear();multiplier->setValue(1);refresh();
      });
      connect(remove,&QPushButton::clicked,&dialog,[&]{
        int rowIndex=table->currentRow();if(rowIndex<0)return;
        const int id=table->item(rowIndex,0)->text().toInt();
        if(QMessageBox::question(&dialog,"Barkodu sil","Seçili alternatif barkod silinsin mi?")!=QMessageBox::Yes)return;
        QString error;if(!run("DELETE FROM product_barcodes WHERE id=? AND product_id=?",{id,selector->currentData()},&error))QMessageBox::warning(&dialog,"Hata",error);
        refresh();
      });
      refresh();dialog.exec();refreshProducts();
    }
    void scanBarcode(){
      if(!barcodeInput)return;
      const QString code=barcodeInput->text().trimmed();barcodeInput->clear();
      if(code.isEmpty())return;
      if(code.size()>64){alert("Barkod çok uzun (en fazla 64 karakter).");barcodeInput->setFocus();return;}
      // V4.1: a physical pack barcode adds the exact base quantity and configured pack price.
      QSqlQuery physicalPack=query("SELECT product_id,id FROM product_packs WHERE barcode=? AND barcode<>'' LIMIT 1",{code});
      if(physicalPack.next()){
        const bool ok=addPackToCart(physicalPack.value(0).toInt(),physicalPack.value(1).toInt());
        if(barcodeState)barcodeState->setText(ok?"✓ Ambalaj sepete eklendi":"Ambalaj satışı tamamlanamadı");
        barcodeInput->setFocus();return;
      }
      // Exact alternative barcode first; multiplier belongs to the barcode, not the product.
      QSqlQuery q=query("SELECT p.id,p.name,pb.unit_multiplier FROM product_barcodes pb JOIN products p ON p.id=pb.product_id WHERE pb.barcode=? LIMIT 1",{code});
      if(!q.next())q=query("SELECT id,name,1 FROM products WHERE barcode=? OR sku=? ORDER BY CASE WHEN barcode=? THEN 0 ELSE 1 END LIMIT 1",{code,code,code});
      if(!q.next()){
        if(barcodeState)barcodeState->setText("Barkod bulunamadı: "+code);
        QApplication::beep();alert("Ürün bulunamadı: "+code+"\nYönetim > Ürün Yönetimi bölümünden barkod tanımlayın.");
        barcodeInput->setFocus();return;
      }
      const int id=q.value(0).toInt();
      const int units=q.value(2).toInt();
      const QString name=q.value(1).toString();
      const bool added=addToCart(id,units);
      if(barcodeState)barcodeState->setText(added?QString("✓ Eklendi: %1 — %2 adet").arg(name).arg(units):"Stok yetersiz: "+name);
      barcodeInput->setFocus();
    }
    void refreshCategories(){wipe(categoryBar);auto *all=new QPushButton("Tüm Ürünler");all->setStyleSheet(currentCategory<0?orangeButton():paleButton());categoryBar->addWidget(all);connect(all,&QPushButton::clicked,this,[this]{currentCategory=-1;refreshCategories();refreshProducts();});
      // Derive categories from existing SKU prefixes until editable categories are added.
      auto q=query("SELECT DISTINCT substr(sku,1,instr(sku||'-','-')-1) FROM products ORDER BY 1 LIMIT 7");int i=0;while(q.next()){QString label=q.value(0).toString();if(label.isEmpty())continue;auto *b=new QPushButton(marketCategory(label));b->setStyleSheet(currentCategory==i?orangeButton():paleButton());categoryBar->addWidget(b);connect(b,&QPushButton::clicked,this,[this,i]{currentCategory=i;refreshCategories();refreshProducts();});++i;}categoryBar->addStretch();}
    void refreshProducts(){
      if(!tileGrid||!search)return;
      wipe(tileGrid);
      const int viewportWidth=productScrollArea?productScrollArea->viewport()->width():900;
      const int cardWidth=156;
      visibleGridColumns=qMax(1,(viewportWidth-10)/(cardWidth+8));
      const QString term=search->text().trimmed();
      QStringList prefixes;
      QSqlQuery categoryQuery=query("SELECT DISTINCT substr(sku,1,instr(sku||'-','-')-1) FROM products ORDER BY 1 LIMIT 7");
      while(categoryQuery.next())if(!categoryQuery.value(0).toString().isEmpty())prefixes<<categoryQuery.value(0).toString();
      QSqlQuery products=query("SELECT p.id,p.name,p.price_cents,p.image_path,p.sku,COALESCE(s.qty-s.reserved,0) FROM products p LEFT JOIN stock s ON s.product_id=p.id AND s.warehouse_id=? WHERE p.name LIKE ? OR p.sku LIKE ? OR p.barcode LIKE ? OR EXISTS (SELECT 1 FROM product_barcodes pb WHERE pb.product_id=p.id AND pb.barcode LIKE ?) ORDER BY p.name",{depotId,"%"+term+"%","%"+term+"%","%"+term+"%","%"+term+"%"});
      int k=0;
      while(products.next()){
        const int id=products.value(0).toInt();
        const QString title=products.value(1).toString();
        const QString sku=products.value(4).toString();
        // Browsing shows style cards; explicit search and barcode still expose individual variants.
        if(term.isEmpty()){
          auto child=query("SELECT 1 FROM product_variants WHERE child_product_id=? LIMIT 1",{id});
          if(child.next())continue;
        }
        if(currentCategory>=0&&currentCategory<prefixes.size()&&!sku.startsWith(prefixes[currentCategory]+"-"))continue;
        const int stock=products.value(5).toInt();
        auto *tile=new QPushButton;
        tile->setFixedSize(cardWidth,152);
        tile->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
        tile->setStyleSheet("QPushButton{background:white;border:1px solid #e3e9ec;border-radius:11px;padding:0px;text-align:center;}QPushButton:hover{border:2px solid #ff542b;background:#fffdfb;}QPushButton:disabled{background:#f5f6f8;color:#9ca3af;}");
        auto *content=new QVBoxLayout(tile);
        content->setContentsMargins(4,4,4,4);
        content->setSpacing(3);
        auto *photo=new QLabel;photo->setFixedHeight(77);photo->setAlignment(Qt::AlignCenter);photo->setAttribute(Qt::WA_TransparentForMouseEvents);
        QPixmap px=picture(products.value(3).toString(),140,76);
        if(px.isNull()){photo->setText("▣");photo->setStyleSheet("font-size:25px;color:#a7b6c6;background:#f1f5f8;border-radius:8px;");}
        else photo->setPixmap(px);
        content->addWidget(photo);
        auto *name=new QLabel(title);name->setFixedHeight(18);name->setAlignment(Qt::AlignCenter);name->setAttribute(Qt::WA_TransparentForMouseEvents);
        name->setStyleSheet("font-size:11px;font-weight:650;background:transparent;");
        name->setText(QFontMetrics(name->font()).elidedText(title,Qt::ElideRight,143));
        name->setToolTip(title);content->addWidget(name);
        auto *price=new QLabel(money(products.value(2).toLongLong()));price->setAlignment(Qt::AlignCenter);
        price->setStyleSheet("color:#00a8a0;font-size:14px;font-weight:850;background:transparent;");
        price->setAttribute(Qt::WA_TransparentForMouseEvents);content->addWidget(price);
        auto *quantity=new QLabel(stock>0?QString("Stok: %1").arg(stock):"Stok yok");
        quantity->setAlignment(Qt::AlignCenter);quantity->setStyleSheet(stock>0?"font-size:10px;color:#8091a4;background:transparent;":"font-size:10px;color:#de3c35;background:transparent;");
        quantity->setAttribute(Qt::WA_TransparentForMouseEvents);content->addWidget(quantity);
        const bool parentWithVariants=hasVariants(id);
        const int stockedVariants=parentWithVariants?availableVariantCount(id):0;
        if(parentWithVariants){
          quantity->setText(QString("%1 stoklu seçenek").arg(stockedVariants));
          quantity->setStyleSheet(stockedVariants>0?"font-size:10px;color:#177e78;background:transparent;":"font-size:10px;color:#de3c35;background:transparent;");
        }
        tile->setEnabled(parentWithVariants?stockedVariants>0:stock>0);
        connect(tile,&QPushButton::clicked,this,[this,id,parentWithVariants]{
          if(parentWithVariants)chooseVariantForSale(id);else addToCart(id);
        });
        tileGrid->addWidget(tile,k/visibleGridColumns,k%visibleGridColumns,Qt::AlignLeft|Qt::AlignTop);++k;
      }
      if(k==0)tileGrid->addWidget(new QLabel("Bu depoda eşleşen ürün bulunamadı."),0,0);
      // Kartların boşluğu doldurup devasa genişlemesini engelle.
      tileGrid->setColumnStretch(visibleGridColumns,1);
      tileGrid->setRowStretch(k/visibleGridColumns+1,1);
    }
    bool eventFilter(QObject *watched,QEvent *event) override {
      if(productScrollArea && watched==productScrollArea->viewport() && event->type()==QEvent::Resize && tileGrid && search){
        const int next=qMax(1,(productScrollArea->viewport()->width()-10)/(156+8));
        if(next!=visibleGridColumns)QTimer::singleShot(0,this,[this,next]{if(tileGrid && search && next!=visibleGridColumns)refreshProducts();});
      }
      return QMainWindow::eventFilter(watched,event);
    }
    void chooseCustomer(){QDialog dlg(this);dlg.setWindowTitle("Cari / Müşteri Seçimi");dlg.resize(570,460);auto *lay=new QVBoxLayout(&dlg);auto *filter=new QLineEdit;filter->setPlaceholderText("Müşteri veya tedarikçi ara...");lay->addWidget(filter);auto *list=new QListWidget;lay->addWidget(list);auto fill=[&]{list->clear();auto q=query("SELECT id,name,kind,balance_cents FROM parties WHERE name LIKE ? ORDER BY name",{"%"+filter->text()+"%"});while(q.next()){auto *item=new QListWidgetItem(QString("%1 · %2 · Bakiye: %3").arg(q.value(1).toString(),q.value(2).toString(),money(q.value(3).toLongLong())));item->setData(Qt::UserRole,q.value(0));item->setData(Qt::UserRole+1,q.value(1));list->addItem(item);}};fill();connect(filter,&QLineEdit::textChanged,&dlg,[&]{fill();});auto *select=new QPushButton("Cariyi seç");select->setStyleSheet(orangeButton());lay->addWidget(select);connect(select,&QPushButton::clicked,&dlg,&QDialog::accept);if(dlg.exec()==QDialog::Accepted && list->currentItem()){customerId=list->currentItem()->data(Qt::UserRole).toInt();customerText->setText(list->currentItem()->data(Qt::UserRole+1).toString());}}
    void hold(){if(basket.isEmpty())return alert("Sepet boş.");bool ok=false;QString label=QInputDialog::getText(this,"Sepeti Beklet","Sepet adı",QLineEdit::Normal,"Bekleyen sepet",&ok);if(!ok)return;
      QJsonObject obj;obj["warehouse_id"]=depotId;obj["customer_id"]=customerId;QJsonArray lines;for(auto &b:basket){QJsonObject row;row["id"]=b.id;row["qty"]=b.qty;row["price_cents"]=QString::number(b.price);row["pack_id"]=b.packId;row["pack_units"]=b.packUnits;lines.append(row);}obj["lines"]=lines;QString blob=QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));if(!run("INSERT INTO held_carts(label,cart_json) VALUES(?,?)",{label,blob}))return alert("Sepet kaydedilemedi.");basket.clear();refreshCart();}
    void restore(){if(!basket.isEmpty())return alert("Önce mevcut sepeti bekletin veya temizleyin.");QDialog d(this);d.setWindowTitle("Bekleyen Sepetler");d.resize(530,430);auto *l=new QVBoxLayout(&d);auto *list=new QListWidget;l->addWidget(list);auto q=query("SELECT id,label,created_at FROM held_carts ORDER BY id DESC");while(q.next()){auto *item=new QListWidgetItem(QString("#%1  %2  ·  %3").arg(q.value(0).toString(),q.value(1).toString(),q.value(2).toString()));item->setData(Qt::UserRole,q.value(0));list->addItem(item);}auto *b=new QPushButton("Seçili Sepeti Aç");b->setStyleSheet(orangeButton());l->addWidget(b);connect(b,&QPushButton::clicked,&d,&QDialog::accept);if(d.exec()!=QDialog::Accepted||!list->currentItem())return;int id=list->currentItem()->data(Qt::UserRole).toInt();auto p=query("SELECT cart_json FROM held_carts WHERE id=?",{id});if(!p.next())return;QJsonDocument data=QJsonDocument::fromJson(p.value(0).toByteArray());QVector<BasketLine> restored;int warehouseId=depotId,party=0;
      if(data.isObject()){auto ob=data.object();warehouseId=ob.value("warehouse_id").toInt(depotId);party=ob.value("customer_id").toInt();for(auto v:ob.value("lines").toArray()){auto row=v.toObject();BasketLine x;x.id=row.value("id").toInt();x.qty=row.value("qty").toInt();x.price=row.value("price_cents").toString().toLongLong();x.packId=row.value("pack_id").toInt();x.packUnits=row.value("pack_units").toInt(1);restored.append(x);}}
      else if(data.isArray()){for(auto v:data.array()){auto row=v.toArray();if(row.size()<2)continue;BasketLine x;x.id=row[0].toInt();x.qty=row[1].toInt();restored.append(x);}}
      for(auto &x:restored){auto pr=query("SELECT name,price_cents,image_path FROM products WHERE id=?",{x.id});if(!pr.next() || x.qty<=0)return alert("Bekleyen sepet ürünlerinden biri bulunamadı.");x.title=pr.value(0).toString();x.image=pr.value(2).toString();if(x.price==0)x.price=pr.value(1).toLongLong();if(x.packId>0){auto pk=query("SELECT label,units_per_pack FROM product_packs WHERE id=? AND product_id=?",{x.packId,x.id});if(!pk.next()||pk.value(1).toInt()!=x.packUnits||x.packUnits<=0||x.qty%x.packUnits!=0)return alert("Bekleyen sepetin ambalaj bilgisi artık geçerli değil.");x.title+=" · "+pk.value(0).toString();}}
      // The held basket remains stored until successfully restored.
      depotId=warehouseId;refreshDepot();customerId=party;auto cust=query("SELECT name FROM parties WHERE id=?",{party});customerText->setText(cust.next()?cust.value(0).toString():"Perakende müşteri");basket=restored;
      if(!run("DELETE FROM held_carts WHERE id=?",{id})){basket.clear();return alert("Bekleyen sepet kaydı silinemedi.");}refreshProducts();refreshCart();}
    void cashShiftPopup(){
      QDialog dlg(this);dlg.setWindowTitle("Kasa Vardiya Merkezi");dlg.resize(600,460);
      auto *layout=new QVBoxLayout(&dlg);auto *summary=new QLabel;summary->setWordWrap(true);layout->addWidget(summary);
      auto *history=new QTableWidget;history->setColumnCount(6);history->setHorizontalHeaderLabels({"ID","Kasiyer","Açılış","Kapanış","Beklenen","Fark"});history->horizontalHeader()->setStretchLastSection(true);layout->addWidget(history);
      auto *buttons=new QHBoxLayout;auto *openButton=new QPushButton("Vardiya Aç");auto *closeButton=new QPushButton("Vardiya Kapat / Sayım");buttons->addWidget(openButton);buttons->addWidget(closeButton);auto *exportButton=new QPushButton("Vardiya Geçmişini CSV Aktar");buttons->addWidget(exportButton);layout->addLayout(buttons);
      auto render=[&](){
        history->setRowCount(0);
        auto records=query("SELECT id,operator_name,opened_at,COALESCE(closed_at,'Açık'),expected_cents,difference_cents FROM cash_shifts ORDER BY id DESC LIMIT 100");
        while(records.next()){int row=history->rowCount();history->insertRow(row);for(int col=0;col<6;++col){QString value=(col==4||col==5)?(records.value(col).isNull()?QString("-"):money(records.value(col).toLongLong())):records.value(col).toString();history->setItem(row,col,new QTableWidgetItem(value));}}
        auto active=query("SELECT id,operator_name,opening_cents,opening_move_id FROM cash_shifts WHERE status='Acik' LIMIT 1");
        if(active.next()){
          const int id=active.value(0).toInt();const qint64 opening=active.value(2).toLongLong();const qint64 anchor=active.value(3).toLongLong();
          auto total=query("SELECT COALESCE(SUM(amount_cents),0) FROM cash_moves WHERE id>?",{anchor});qint64 change=total.next()?total.value(0).toLongLong():0;
          summary->setText(QString("AÇIK VARDİYA #%1 · %2\nAçılış: %3 · Nakit hareket: %4 · Beklenen: %5")
            .arg(id).arg(active.value(1).toString()).arg(money(opening),money(change),money(opening+change)));
          openButton->setEnabled(false);closeButton->setEnabled(true);
        }else{summary->setText("Açık vardiya bulunmuyor. Yeni vardiya açabilirsiniz.");openButton->setEnabled(true);closeButton->setEnabled(false);}
      };
      connect(exportButton,&QPushButton::clicked,&dlg,[&](){
        const QString path=QFileDialog::getSaveFileName(&dlg,"Vardiya raporu", "kasapro_vardiya_raporu.csv", "CSV (*.csv)");if(path.isEmpty())return;
        QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(&dlg,"CSV","Rapor dosyası açılamadı.");return;}
        QTextStream stream(&file);stream.setEncoding(QStringConverter::Utf8);stream<<QChar(0xFEFF);
        stream<<"Vardiya;Kasiyer;Açılış;Kapanış;Açılış Nakit;Beklenen;Sayılan;Fark;Durum\n";
        auto records=query("SELECT id,operator_name,opened_at,COALESCE(closed_at,''),opening_cents,expected_cents,counted_cents,difference_cents,status FROM cash_shifts ORDER BY id DESC");
        if(records.lastError().isValid()){QMessageBox::warning(&dlg,"CSV","Vardiya bilgileri okunamadı.");return;}
        while(records.next()){
          QStringList values;
          for(int index=0;index<9;++index){
             QString value=(index>=4&&index<=7&&!records.value(index).isNull())?money(records.value(index).toLongLong()):records.value(index).toString();
             value.replace('"',"""");values<<('"'+value+'"');
          }
          stream<<values.join(';')<<'\n';
        }
        QMessageBox::information(&dlg,"CSV","Vardiya raporu kaydedildi.");
      });
      connect(openButton,&QPushButton::clicked,&dlg,[&](){
        QDialog form(this);form.setWindowTitle("Kasa Açılışı");auto *v=new QFormLayout(&form);
        auto *operatorInput=new QLineEdit;auto *opening=new QDoubleSpinBox;opening->setRange(0,100000000);opening->setDecimals(2);opening->setSuffix(" TL");
        auto *confirm=new QPushButton("Vardiyayı Aç");v->addRow("Kasiyer",operatorInput);v->addRow("Sayılmış açılış nakdi",opening);v->addRow(confirm);
        connect(confirm,&QPushButton::clicked,&form,&QDialog::accept);
        if(form.exec()!=QDialog::Accepted)return;
        if(operatorInput->text().trimmed().isEmpty()){alert("Kasiyer adı boş olamaz.");return;}
        if(!db.transaction()){alert("Veritabanı işlemi başlatılamadı.");return;}
        auto last=query("SELECT COALESCE(MAX(id),0) FROM cash_moves");qint64 anchor=0;if(last.next())anchor=last.value(0).toLongLong();
        const bool ok=run("INSERT INTO cash_shifts(operator_name,opening_cents,opening_move_id) VALUES(?,?,?)",{operatorInput->text().trimmed(),toCents(opening->value()),anchor});
        if(ok&&db.commit())render();else{db.rollback();alert("Vardiya açılamadı. Zaten açık vardiya olabilir.");}
      });
      connect(closeButton,&QPushButton::clicked,&dlg,[&](){
        auto active=query("SELECT id,opening_cents,opening_move_id FROM cash_shifts WHERE status='Acik' LIMIT 1");if(!active.next())return;
        const int id=active.value(0).toInt();const qint64 opening=active.value(1).toLongLong();const qint64 anchor=active.value(2).toLongLong();
        auto movement=query("SELECT COALESCE(SUM(amount_cents),0) FROM cash_moves WHERE id>?",{anchor});qint64 net=movement.next()?movement.value(0).toLongLong():0;const qint64 expected=opening+net;
        QDialog form(this);form.setWindowTitle("Vardiya Kapanışı");auto *v=new QFormLayout(&form);auto *expectedLabel=new QLabel(money(expected));
        auto *counted=new QDoubleSpinBox;counted->setRange(0,100000000);counted->setDecimals(2);counted->setValue(qMax(0.0,expected/100.0));counted->setSuffix(" TL");
        auto *confirm=new QPushButton("Kapanışı Kaydet");v->addRow("Beklenen nakit",expectedLabel);v->addRow("Sayılan nakit",counted);v->addRow(confirm);
        connect(confirm,&QPushButton::clicked,&form,&QDialog::accept);if(form.exec()!=QDialog::Accepted)return;
        const qint64 physical=toCents(counted->value());
        if(physical!=expected && !SupervisorGate::authorize(&form,db,QString("Vardiya #%1 kasa farkı: %2").arg(id).arg(money(physical-expected)),id))return;
        if(!db.transaction()){alert("Veritabanı işlemi başlatılamadı.");return;}
        bool ok=run("UPDATE cash_shifts SET closed_at=CURRENT_TIMESTAMP,status='Kapali',expected_cents=?,counted_cents=?,difference_cents=? WHERE id=? AND status='Acik'",{expected,physical,physical-expected,id});
        auto changes=query("SELECT changes()");ok=ok&&changes.next()&&changes.value(0).toInt()==1;
        if(ok&&db.commit()){render();QMessageBox::information(this,"Vardiya kapandı","Kasa farkı: "+money(physical-expected));}
        else{db.rollback();alert("Kasa vardiyası kapatılamadı.");}
      });render();dlg.exec();
    }
    void pay(){
      if(basket.isEmpty())return alert("Sepet boş.");
      const qint64 totalCents=cartTotal();
      QDialog dlg(this);dlg.setWindowTitle("Parçalı Ödeme");dlg.setMinimumSize(480,420);auto *v=new QVBoxLayout(&dlg);
      auto *heading=new QLabel("ÖDENECEK  "+money(totalCents));heading->setAlignment(Qt::AlignCenter);heading->setStyleSheet("font-size:25px;font-weight:800;color:#139e9a;");v->addWidget(heading);
      auto *form=new QFormLayout;auto *cash=new QDoubleSpinBox;auto *card=new QDoubleSpinBox;auto *credit=new QDoubleSpinBox;
      for(QDoubleSpinBox *spin:{cash,card,credit}){spin->setDecimals(2);spin->setRange(0,100000000);spin->setSuffix(" TL");}
      cash->setValue(totalCents/100.0);form->addRow("Nakit",cash);form->addRow("Kart",card);form->addRow("Veresiye",credit);v->addLayout(form);
      auto *remaining=new QLabel;v->addWidget(remaining);
      auto recalc=[&](){const qint64 paid=toCents(cash->value())+toCents(card->value())+toCents(credit->value());remaining->setText("Fark: "+money(totalCents-paid)+(paid==totalCents?"  ✓":"  (tutarlar eşit olmalı)"));};
      connect(cash,QOverload<double>::of(&QDoubleSpinBox::valueChanged),&dlg,[&](double){recalc();});
      connect(card,QOverload<double>::of(&QDoubleSpinBox::valueChanged),&dlg,[&](double){recalc();});
      connect(credit,QOverload<double>::of(&QDoubleSpinBox::valueChanged),&dlg,[&](double){recalc();});recalc();
      // Test arayüzü: entegrasyon protokolü eklenene kadar GERÇEK cihaz tahsilatı iddia edilmez.
      auto *devicePanel=new QGroupBox("Ödeme Cihazı — Test Bağlantısı");
      auto *deviceLayout=new QVBoxLayout(devicePanel);
      auto *deviceInfo=new QLabel("Henüz üretici SDK'sı bağlı değil. Simülasyon gerçek ödeme yapmaz.");
      deviceInfo->setWordWrap(true);deviceLayout->addWidget(deviceInfo);
      auto *devicePicker=new QComboBox;
      const QVector<DeviceProfile> profiles=deviceCatalog();
      for(const DeviceProfile &profile: profiles)
          devicePicker->addItem(describeProfile(profile),profile.id);
      deviceLayout->addWidget(devicePicker);
      auto *centerButton=new QPushButton("Cihaz Yönetimi ve Mutabakat");
      centerButton->setToolTip("Cihaz profilleri, TCP erişimi ve belirsiz ödeme kayıtları.");
      deviceLayout->addWidget(centerButton);
      connect(centerButton,&QPushButton::clicked,&dlg,[&](){
        DeviceConnectionCenter center(db,&dlg);
        center.exec();
      });
      auto *historyButton=new QPushButton("Cihaz İşlem Geçmişi");
      historyButton->setToolTip("Cihaza gönderim denemelerini ve durumlarını gösterir; tahsilat raporu değildir.");
      deviceLayout->addWidget(historyButton);
      connect(historyButton,&QPushButton::clicked,&dlg,[&](){
        QDialog history(&dlg);history.setWindowTitle("Ödeme Cihazı — İşlem Geçmişi");history.resize(950,520);
        auto *layout=new QVBoxLayout(&history);
        auto *explanation=new QLabel("Bu kayıtlar cihaz iletişim denemeleridir. 'Bekliyor' veya 'Bilinmiyor' tahsilat onayı değildir.");
        explanation->setWordWrap(true);layout->addWidget(explanation);
        auto *filter=new QComboBox;filter->addItems({"Tüm durumlar","Bekliyor","Bilinmiyor","Hata","Reddedildi","Onaylandi"});layout->addWidget(filter);
        auto *grid=new QTableWidget;grid->setColumnCount(7);
        grid->setHorizontalHeaderLabels({"Tarih","Referans","Profil","Tutar (TL)","Durum","Mali fiş","Açıklama"});
        grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
        grid->setSelectionBehavior(QAbstractItemView::SelectRows);
        grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(grid);
        auto render=[&](){
          grid->setRowCount(0);
          QString sql="SELECT created_at,merchant_reference,profile_id,amount_cents,status,fiscal_documented,message FROM device_payment_attempts";
          QVariantList args;
          if(filter->currentIndex()>0){sql+=" WHERE status=?";args.append(filter->currentText());}
          sql+=" ORDER BY id DESC LIMIT 300";
          QSqlQuery records=query(sql,args);
          while(records.next()){
            const int row=grid->rowCount();grid->insertRow(row);
            for(int col=0;col<7;++col){
              QString value=records.value(col).toString();
              if(col==3)value=money(records.value(col).toLongLong());
              if(col==5)value=records.value(col).toBool()?"Evet":"Hayır";
              grid->setItem(row,col,new QTableWidgetItem(value));
            }
          }
        };
        connect(filter,QOverload<int>::of(&QComboBox::currentIndexChanged),&history,[&](int){render();});
        auto *close=new QPushButton("Kapat");layout->addWidget(close);
        connect(close,&QPushButton::clicked,&history,&QDialog::accept);
        render();history.exec();
      });
      auto *deviceTest=new QPushButton("Cihaz Bağlantısını Kontrol Et / Test");
      deviceLayout->addWidget(deviceTest);
      connect(devicePicker,QOverload<int>::of(&QComboBox::currentIndexChanged),&dlg,[=](int index){
          if(index < 0 || index >= profiles.size()) return;
          const DeviceProfile &p=profiles.at(index);
          deviceInfo->setText(p.id=="demo" ?
              "Simülasyon: banka tahsilatı ve mali fiş YOK." :
              p.brand+" "+p.model+" için üretici SDK/servis etkinleştirmesi gerekiyor. KasaPro henüz bu cihaza ödeme göndermez.");
      });
      connect(deviceTest,&QPushButton::clicked,&dlg,[&](){
        const int index=devicePicker->currentIndex();
        if(index<0 || index>=profiles.size())return;
        const DeviceProfile profile=profiles.at(index);
        const qint64 amount=toCents(card->value());
        if(amount<=0){QMessageBox::warning(&dlg,"Cihaz testi","Önce kart ödeme tutarını girin.");return;}
        DevicePaymentRequest request{createPaymentReference(),amount,QStringLiteral("TRY")};
        std::unique_ptr<IPaymentDevice> driver;
        if(profile.id=="demo") driver=std::make_unique<DemoPaymentDevice>();
        else driver=std::make_unique<UnavailableDevice>(profile);
        QString storeError;
        if(!PaymentAttemptStore::create(db,request,profile.id,&storeError)){
          QMessageBox::warning(&dlg,"Cihaz kaydı","İşlem günlüğü oluşturulamadı: "+storeError);return;
        }
        const DevicePaymentResult result=driver->startPayment(request);
        if(!PaymentAttemptStore::update(db,request.merchantReference,result,&storeError)){
          QMessageBox::warning(&dlg,"Cihaz kaydı","İşlem sonucu kaydedilemedi. Referans: "+request.merchantReference+"\n"+storeError);
          return;
        }
        const QString auditDir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(auditDir);
        writeDeviceAudit(auditDir+"/device_attempts.jsonl",profile.id,request,result);
        deviceInfo->setText(result.message+"\nReferans: "+request.merchantReference);
        QMessageBox::information(&dlg,"Cihaz durumu",result.message+"\nHiçbir gerçek banka tahsilatı başlatılmadı.");
      });
      v->addWidget(devicePanel);
      auto *confirm=new QPushButton("Satışı Tamamla (Manuel ödeme kaydı)");confirm->setStyleSheet(orangeButton());v->addWidget(confirm);
      connect(confirm,&QPushButton::clicked,&dlg,[&](){
        if(toCents(cash->value())+toCents(card->value())+toCents(credit->value())!=totalCents){QMessageBox::warning(&dlg,"Ödeme","Ödeme tutarları satış toplamına eşit olmalı.");return;}
        if(credit->value()>0 && customerId<=0){QMessageBox::warning(&dlg,"Cari","Veresiye için müşteri seçiniz.");return;}dlg.accept();
      });
      if(dlg.exec()!=QDialog::Accepted)return;
      const qint64 cashCents=toCents(cash->value());const qint64 cardCents=toCents(card->value());const qint64 creditCents=toCents(credit->value());
      if(!db.transaction())return alert("Veritabanı işlemi açılamadı.");
      bool ok=true;int docId=0;QVariant party=customerId>0?QVariant(customerId):QVariant();
      ok=run("INSERT INTO docs(doc_type,status,party_id,total_cents) VALUES('Satis','Tamamlandi',?,?)",{party,totalCents});
      if(ok){auto q=query("SELECT last_insert_rowid()");if(q.next())docId=q.value(0).toInt();else ok=false;}
      for(const BasketLine &line:basket){
        if(!ok)break;
        QString physicalError;ok=CartonSaleEngine::consume(db,line.id,depotId,line.qty,docId,&physicalError);
        if(!ok){qWarning()<<"Koli satış kontrolü:"<<physicalError;break;}
        ok=run("UPDATE stock SET qty=qty-? WHERE product_id=? AND warehouse_id=? AND qty-reserved>=?",{line.qty,line.id,depotId,line.qty});
        auto changes=query("SELECT changes()");ok=ok&&changes.next()&&changes.value(0).toInt()==1;
        if(ok)ok=run("INSERT INTO doc_lines(doc_id,product_id,warehouse_id,qty,unit_cents) VALUES(?,?,?,?,?)",{docId,line.id,depotId,line.qty,line.price});
        if(ok)ok=run("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason,doc_id) VALUES(?,?,?,'Satis',?)",{line.id,depotId,-line.qty,docId});
      }
      if(ok&&cashCents>0)ok=run("INSERT INTO cash_moves(kind,amount_cents,description,doc_id) VALUES('Satis',?,'POS nakit tahsilat',?)",{cashCents,docId});
      if(ok&&creditCents>0)ok=run("UPDATE parties SET balance_cents=balance_cents+? WHERE id=?",{creditCents,customerId});
      if(ok&&creditCents>0)ok=run("INSERT INTO party_movements(party_id,kind,amount_cents,description) VALUES(?,'Veresiye Satis',? ,?)",{customerId,creditCents,QString("POS belge #%1").arg(docId)});
      for(const auto &entry:std::initializer_list<std::pair<QString,qint64>>{{"Nakit",cashCents},{"Kart",cardCents},{"Veresiye",creditCents}}){
        if(ok&&entry.second>0)ok=run("INSERT INTO pos_payments(doc_id,method,amount_cents) VALUES(?,?,?)",{docId,entry.first,entry.second});
      }
      if(ok&&db.commit()){
        basket.clear();refreshCart();refreshProducts();
        QMessageBox::information(this,"Satış tamamlandı",QString("Satış #%1\nNakit: %2\nKart: %3\nVeresiye: %4").arg(docId).arg(money(cashCents),money(cardCents),money(creditCents)));
      }else{db.rollback();alert("Satış kaydedilemedi. İşlem geri alındı.");}
    }
    bool posFullscreen=false;
    QPushButton *posFullscreenButton=nullptr;
    void togglePosFullscreen(){posFullscreen=!isFullScreen();if(posFullscreen)showFullScreen();else showNormal();if(posFullscreenButton)posFullscreenButton->setText(posFullscreen?"❐ Pencere":"⛶ Tam Ekran");QSettings settings("KasaPro","KasaPro");settings.setValue("ui/posFullscreen",posFullscreen);}
    void officePopup(const QString &section){ManagementCenter dialog(section,this);dialog.exec();refreshDepot();refreshCategories();refreshProducts();refreshCart();}
    QPushButton *action(const QString &name,const QString &color="#ff542b"){auto *b=new QPushButton(name);b->setMinimumHeight(53);b->setStyleSheet(QString("QPushButton{background:%1;color:white;border:0;border-radius:11px;font-weight:750;padding:8px;}QPushButton:hover{border:2px solid #273444;}").arg(color));return b;}
public:
    PosWindow(){setWindowTitle("KasaPro Enterprise | POS V2.6");resize(1460,870);setMinimumSize(900,620);setStyleSheet("QMainWindow{background:#f4f6f8;} QWidget{font-family:'Segoe UI';font-size:12px;color:#293442;} QScrollArea{border:0;} QLineEdit,QComboBox{background:white;border:1px solid #dfe5eb;border-radius:9px;padding:9px;} QDialog{background:#f8fafc;}");
      auto *base=new QWidget;
      setCentralWidget(base);
      auto *root=new QVBoxLayout(base);
      root->setContentsMargins(9,9,9,9);
      root->setSpacing(8);
      auto *top=new QFrame;
      top->setObjectName("posHeader");
      top->setStyleSheet("QFrame#posHeader{background:white;border-radius:12px;}");
      auto *topColumn=new QVBoxLayout(top);
      topColumn->setContentsMargins(14,9,14,9);
      topColumn->setSpacing(8);
      auto *identityRow=new QHBoxLayout;
      auto *logo=new QLabel("<span style='color:#fa502d;font-size:26px;font-weight:900'>KasaPro</span>");
      identityRow->addWidget(logo);
      auto *order=new QLabel("POS  •  Mağaza içi satış");
      identityRow->addSpacing(14);
      identityRow->addWidget(order);
      identityRow->addStretch(1);
      clockLabel=new QLabel;
      clockLabel->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
      identityRow->addWidget(clockLabel);
      posFullscreenButton=new QPushButton("⛶ Tam Ekran");
      posFullscreenButton->setStyleSheet(paleButton());
      identityRow->addWidget(posFullscreenButton);
      connect(posFullscreenButton,&QPushButton::clicked,this,[this]{togglePosFullscreen();});
      auto *manage=new QPushButton("☰ Yönetim");
      manage->setStyleSheet(orangeButton());
      identityRow->addWidget(manage);
      connect(manage,&QPushButton::clicked,this,[this]{officePopup("Yönetim Merkezi");});
      topColumn->addLayout(identityRow);
      auto *controlsRow=new QHBoxLayout;
      controlsRow->setSpacing(9);
      customerText=new QLabel("Perakende müşteri");
      customerText->setMinimumWidth(0);
      customerText->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
      customerText->setToolTip("Seçili müşteri");
      controlsRow->addWidget(customerText,1);
      auto *customerButton=new QPushButton("Müşteri Seç");
      customerButton->setStyleSheet(paleButton());
      controlsRow->addWidget(customerButton);
      connect(customerButton,&QPushButton::clicked,this,[this]{chooseCustomer();});
      controlsRow->addWidget(new QLabel("Depo:"));
      depotPicker=new QComboBox;
      depotPicker->setMinimumWidth(145);
      depotPicker->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
      controlsRow->addWidget(depotPicker,1);
      auto *shiftButton=new QPushButton("Kasa Vardiyası");
      shiftButton->setStyleSheet(paleButton());
      controlsRow->addWidget(shiftButton);
      connect(shiftButton,&QPushButton::clicked,this,[this]{cashShiftPopup();});
      topColumn->addLayout(controlsRow);
      auto *timer=new QTimer(this);
      connect(timer,&QTimer::timeout,this,[this]{clockLabel->setText(QDateTime::currentDateTime().toString("dd.MM.yyyy  HH:mm"));});
      timer->start(15000);
      clockLabel->setText(QDateTime::currentDateTime().toString("dd.MM.yyyy  HH:mm"));
      root->addWidget(top);
      auto *content=new QHBoxLayout;
      content->setSpacing(10);
      root->addLayout(content,1);

      auto *left=new QFrame;left->setMinimumWidth(300);left->setMaximumWidth(450);left->setStyleSheet("QFrame{background:white;border-radius:13px;}");content->addWidget(left,4);auto *lv=new QVBoxLayout(left);auto *cartTop=new QHBoxLayout;auto *cartTitle=new QLabel("SEPET");cartTitle->setStyleSheet("font-weight:800;font-size:16px");cartTop->addWidget(cartTitle);cartTop->addStretch();countLabel=new QLabel;cartTop->addWidget(countLabel);lv->addLayout(cartTop);auto *cartScroll=new QScrollArea;cartScroll->setWidgetResizable(true);cartHolder=new QWidget;cartRows=new QVBoxLayout(cartHolder);cartRows->setContentsMargins(0,0,0,0);cartScroll->setWidget(cartHolder);lv->addWidget(cartScroll,1);auto *s1=new QHBoxLayout;s1->addWidget(new QLabel("Ara toplam"));s1->addStretch();subtotal=new QLabel;s1->addWidget(subtotal);lv->addLayout(s1);auto *g=new QFrame;g->setStyleSheet("background:#e9f7f6;border-radius:10px;");auto *gl=new QHBoxLayout(g);gl->addWidget(new QLabel("GENEL TOPLAM"));gl->addStretch();grand=new QLabel;grand->setStyleSheet("font-size:20px;font-weight:900;color:#16a7a2;");gl->addWidget(grand);lv->addWidget(g);auto *quick=new QGridLayout;auto *holdBtn=action("Beklet","#7163c9");auto *restoreBtn=action("Bekleyenler","#666a77");auto *clearBtn=action("Temizle","#ef6946");auto *payBtn=action("ÖDEME AL","#25a33b");quick->addWidget(holdBtn,0,0);quick->addWidget(restoreBtn,0,1);quick->addWidget(clearBtn,1,0);quick->addWidget(payBtn,1,1);lv->addLayout(quick);connect(holdBtn,&QPushButton::clicked,this,[this]{hold();});connect(restoreBtn,&QPushButton::clicked,this,[this]{restore();});connect(clearBtn,&QPushButton::clicked,this,[this]{basket.clear();refreshCart();});connect(payBtn,&QPushButton::clicked,this,[this]{pay();});
      auto *right=new QFrame;right->setStyleSheet("QFrame{background:#f4f6f8;border-radius:12px;}");content->addWidget(right,7);auto *rv=new QVBoxLayout(right);rv->setContentsMargins(4,4,4,4);auto *categoryScroll=new QScrollArea;categoryScroll->setWidgetResizable(true);categoryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);categoryScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);categoryScroll->setFixedHeight(62);
      auto *categoryWidget=new QWidget;categoryBar=new QHBoxLayout(categoryWidget);categoryBar->setContentsMargins(3,3,3,3);categoryWidget->setMinimumWidth(690);categoryScroll->setWidget(categoryWidget);rv->addWidget(categoryScroll);search=new QLineEdit;search->setPlaceholderText("Ürün adı veya barkod / stok kodu ara...");search->setMinimumHeight(42);rv->addWidget(search);
      auto *scanRow=new QHBoxLayout;
      auto *scanIcon=new QLabel("▥  BARKOD");scanIcon->setStyleSheet("color:#e6532e;font-weight:800;");scanRow->addWidget(scanIcon);
      barcodeInput=new QLineEdit;barcodeInput->setObjectName("barcodeInput");barcodeInput->setPlaceholderText("Barkodu okutun / yazın ve Enter'a basın");barcodeInput->setMinimumHeight(44);barcodeInput->setMaxLength(64);barcodeInput->setStyleSheet("QLineEdit{background:#fff8f5;border:2px solid #f3a38b;border-radius:9px;padding:8px 12px;font-weight:700;}QLineEdit:focus{border:2px solid #f15a32;}");scanRow->addWidget(barcodeInput,1);
      auto *scanGo=new QPushButton("Ürünü Ekle");scanGo->setStyleSheet(orangeButton());scanRow->addWidget(scanGo);
      auto *focusScan=new QPushButton("F8 Barkod");focusScan->setStyleSheet(paleButton());scanRow->addWidget(focusScan);
      auto *packSales=new QPushButton("Koli / Paket Sat");packSales->setStyleSheet(paleButton());scanRow->addWidget(packSales);connect(packSales,&QPushButton::clicked,this,[this]{choosePackForSale();});
      auto *barcodeSettings=new QPushButton("Barkod Yönetimi");barcodeSettings->setStyleSheet(paleButton());scanRow->addWidget(barcodeSettings);connect(barcodeSettings,&QPushButton::clicked,this,[this]{barcodeManager();});
      rv->addLayout(scanRow);barcodeState=new QLabel("USB barkod okuyucuyu klavye/HID modunda kullanın (Enter sonlandırıcı).");barcodeState->setStyleSheet("color:#65768b;font-size:11px;");rv->addWidget(barcodeState);
      connect(barcodeInput,&QLineEdit::returnPressed,this,[this]{scanBarcode();});connect(scanGo,&QPushButton::clicked,this,[this]{scanBarcode();});
      auto *screenShortcut=new QShortcut(QKeySequence(Qt::Key_F11),this);connect(screenShortcut,&QShortcut::activated,this,[this]{togglePosFullscreen();});
            auto *scanShortcut=new QShortcut(QKeySequence(Qt::Key_F8),this);connect(scanShortcut,&QShortcut::activated,this,[this]{barcodeInput->setFocus();barcodeInput->selectAll();});
      connect(focusScan,&QPushButton::clicked,this,[this]{barcodeInput->setFocus();barcodeInput->selectAll();});
      auto *productScroll=new QScrollArea;productScrollArea=productScroll;productScroll->setWidgetResizable(true);productScroll->viewport()->installEventFilter(this);tiles=new QWidget;tileGrid=new QGridLayout(tiles);tileGrid->setContentsMargins(2,9,2,9);tileGrid->setHorizontalSpacing(8);tileGrid->setVerticalSpacing(9);tileGrid->setAlignment(Qt::AlignLeft|Qt::AlignTop);productScroll->setWidget(tiles);rv->addWidget(productScroll,1);auto *toolsScroll=new QScrollArea;toolsScroll->setWidgetResizable(true);toolsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);toolsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);toolsScroll->setFixedHeight(68);
      auto *toolsWidget=new QWidget;auto *tools=new QHBoxLayout(toolsWidget);tools->setContentsMargins(4,4,4,4);for(const QString &name:{"Cari Kartlar","Kasa","Depo / Stok","Siparişler","Satın Alma","Personel","Raporlar"}){auto *b=new QPushButton(name);b->setStyleSheet(paleButton());tools->addWidget(b);connect(b,&QPushButton::clicked,this,[this,name]{officePopup(name);});}auto *salesHistory=new QPushButton("Satışlar / İadeler");salesHistory->setStyleSheet(paleButton());tools->addWidget(salesHistory);connect(salesHistory,&QPushButton::clicked,this,[this]{salesReturnCenter(this);refreshProducts();refreshCart();});toolsWidget->setMinimumWidth(750);toolsScroll->setWidget(toolsWidget);rv->addWidget(toolsScroll);connect(search,&QLineEdit::textChanged,this,[this]{refreshProducts();});refreshDepot();connect(depotPicker,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){int next=depotPicker->currentData().toInt();if(next==depotId)return;if(!basket.isEmpty()){QSignalBlocker guard(depotPicker);int previous=depotPicker->findData(depotId);if(previous>=0)depotPicker->setCurrentIndex(previous);alert("Önce sepeti tamamlayın veya bekletin. Satış deposu sepet doluyken değiştirilemez.");return;}depotId=next;refreshProducts();});refreshCategories();refreshProducts();refreshCart();}
};

int main(int argc,char **argv){QApplication app(argc,argv);
    app.setStyleSheet(UiDesign::globalQss() + QString::fromUtf8(R"QSS(
       QDialog { background:#f6f8fb; color:#26364b; font-family:'Segoe UI'; font-size:13px; }
       QDialog QLabel { color:#26364b; }
       QDialog QLineEdit,QDialog QComboBox,QDialog QSpinBox,QDialog QDoubleSpinBox,QDialog QDateEdit { background:#ffffff; border:1px solid #d4dfeb; border-radius:8px; padding:8px; min-height:20px; }
       QDialog QPushButton { background:#f15a32; color:white; border:0; border-radius:9px; padding:10px 14px; min-height:22px; font-weight:600; }
       QDialog QPushButton:hover { background:#dd4825; }
       QDialog QTableWidget { background:white; alternate-background-color:#f2f6fa; border:1px solid #dce5ed; gridline-color:#e9eff4; selection-background-color:#fbe4dd; selection-color:#25354b; }
       QDialog QHeaderView::section { background:#eaf0f7; padding:10px; color:#283b54; border:0; font-weight:700; }
    )QSS"));app.setApplicationName("KasaPro");app.setOrganizationName("KasaPro");if(!initDb()){QMessageBox::critical(nullptr,"Veritabanı","SQLite açılamadı: "+db.lastError().text());return 1;}if(!initSalesReturns()){QMessageBox::critical(nullptr,"İade şeması",db.lastError().text());return 1;}QString supervisorError;if(!SupervisorGate::initialize(db,&supervisorError)){QMessageBox::critical(nullptr,"Yetkilendirme",supervisorError);return 1;}PosWindow window;QSettings uiSettings("KasaPro","KasaPro");if(uiSettings.value("ui/posFullscreen",false).toBool())window.showFullScreen();else window.show();return app.exec();}
