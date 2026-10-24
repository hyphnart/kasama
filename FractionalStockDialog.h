#pragma once
#include <QtWidgets>
#include <QtSql>
#include <limits>

// Independent fixed-point fractional-stock ledger. Does not alter the integer POS engine.
namespace FractionalStock {
inline bool install(QSqlDatabase db,QString *error=nullptr){
 const QStringList ddl={
  "CREATE TABLE IF NOT EXISTS fractional_stock_settings(product_id INTEGER PRIMARY KEY REFERENCES products(id),unit TEXT NOT NULL CHECK(unit IN ('Metre','Kg','Litre','m²','m³')),created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",
  "CREATE TABLE IF NOT EXISTS fractional_stock_balances(product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),quantity_milli INTEGER NOT NULL DEFAULT 0 CHECK(quantity_milli>=0),PRIMARY KEY(product_id,warehouse_id))",
  "CREATE TABLE IF NOT EXISTS fractional_stock_movements(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),delta_milli INTEGER NOT NULL CHECK(delta_milli!=0),reason TEXT NOT NULL CHECK(length(trim(reason))>0),created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",
  "CREATE INDEX IF NOT EXISTS ix_fractional_stock_movements_lookup ON fractional_stock_movements(product_id,warehouse_id,id)"};
 for(const auto &sql:ddl){QSqlQuery q(db);if(!q.exec(sql)){if(error)*error=q.lastError().text();return false;}}return true;
}
inline QString amount(qint64 milli){bool neg=milli<0;quint64 n=neg?quint64(-(milli+1))+1:quint64(milli);return QString("%1%2,%3").arg(neg?"-":"").arg(n/1000).arg(n%1000,3,10,QChar('0'));}
inline bool post(QSqlDatabase db,int pid,int wid,qint64 delta,const QString &reason,QString *error=nullptr){
 auto fail=[&](const QString&msg){if(error)*error=msg;return false;};
 if(pid<=0||wid<=0||delta==0||reason.trimmed().isEmpty())return fail("Ürün, depo, miktar ve açıklama zorunludur.");
 if(!db.transaction())return fail(db.lastError().text());
 QSqlQuery registered(db);registered.prepare("SELECT 1 FROM fractional_stock_settings WHERE product_id=?");registered.addBindValue(pid);
 if(!registered.exec()||!registered.next()){db.rollback();return fail("Ürün kesirli stok için etkinleştirilmemiş.");}
 QSqlQuery ensure(db);ensure.prepare("INSERT OR IGNORE INTO fractional_stock_balances(product_id,warehouse_id,quantity_milli) VALUES(?,?,0)");ensure.addBindValue(pid);ensure.addBindValue(wid);
 if(!ensure.exec()){db.rollback();return fail(ensure.lastError().text());}
 // SQL guarded update prevents negative balances and integer overflow.
 QSqlQuery upd(db);upd.prepare("UPDATE fractional_stock_balances SET quantity_milli=quantity_milli+? WHERE product_id=? AND warehouse_id=? AND quantity_milli+?>=0 AND ((?>0 AND quantity_milli<=9223372036854775807-?) OR ?<0)");
 for(const QVariant &v:QVariantList{delta,pid,wid,delta,delta,delta,delta})upd.addBindValue(v);
 if(!upd.exec()||upd.numRowsAffected()!=1){db.rollback();return fail("Yetersiz miktar veya taşma. İşlem yapılmadı.");}
 QSqlQuery movement(db);movement.prepare("INSERT INTO fractional_stock_movements(product_id,warehouse_id,delta_milli,reason) VALUES(?,?,?,?)");for(const QVariant &v:QVariantList{pid,wid,delta,reason.trimmed()})movement.addBindValue(v);
 if(!movement.exec()){db.rollback();return fail(movement.lastError().text());}
 if(!db.commit()){db.rollback();return fail(db.lastError().text());}return true;
}
}

class FractionalStockDialog final:public QDialog {
 QSqlDatabase db; QComboBox *product=nullptr,*warehouse=nullptr,*unit=nullptr;QDoubleSpinBox *quantity=nullptr;QLineEdit *reason=nullptr;QTableWidget *history=nullptr;QLabel *balance=nullptr;
 void updateView(){
  const int pid=product->currentData().toInt(),wid=warehouse->currentData().toInt();
  QSqlQuery settings(db);settings.prepare("SELECT unit FROM fractional_stock_settings WHERE product_id=?");settings.addBindValue(pid);bool enabled=settings.exec()&&settings.next();
  unit->setEnabled(!enabled);if(enabled)unit->setCurrentText(settings.value(0).toString());
  QSqlQuery q(db);q.prepare("SELECT quantity_milli FROM fractional_stock_balances WHERE product_id=? AND warehouse_id=?");q.addBindValue(pid);q.addBindValue(wid);q.exec();qint64 current=q.next()?q.value(0).toLongLong():0;
  balance->setText((enabled?"Kesirli defter bakiyesi: ":"Kesirli takip henüz açılmadı · Bakiye: ")+FractionalStock::amount(current)+" "+unit->currentText());
  history->setRowCount(0);QSqlQuery hist(db);hist.prepare("SELECT created_at,delta_milli,reason FROM fractional_stock_movements WHERE product_id=? AND warehouse_id=? ORDER BY id DESC LIMIT 100");hist.addBindValue(pid);hist.addBindValue(wid);if(hist.exec())while(hist.next()){int row=history->rowCount();history->insertRow(row);history->setItem(row,0,new QTableWidgetItem(hist.value(0).toString()));history->setItem(row,1,new QTableWidgetItem(FractionalStock::amount(hist.value(1).toLongLong())));history->setItem(row,2,new QTableWidgetItem(hist.value(2).toString()));}
 }
public:
 explicit FractionalStockDialog(QSqlDatabase database,QWidget *parent=nullptr):QDialog(parent),db(database){
  setWindowTitle("KasaPro | Kesirli Stok Defteri (Pilot)");resize(850,670);setMinimumSize(650,530);
  auto *layout=new QVBoxLayout(this);layout->setContentsMargins(20,20,20,20);layout->setSpacing(12);
  auto *title=new QLabel("Kesirli Stok Takibi");title->setStyleSheet("font-size:22px;font-weight:700;color:#1a344a");layout->addWidget(title);
  auto *warning=new QLabel("Bu modül metre/kg/litre için bağımsız stok defteridir. POS satışı, iade, satın alma ve ana tam sayı stoklarını DEĞİŞTİRMEZ. Aynı ürün için iki ayrı stok defterini eş zamanlı kullanmayın.");warning->setWordWrap(true);warning->setStyleSheet("background:#fff4df;color:#714200;padding:12px;border-radius:8px;");layout->addWidget(warning);
  auto *form=new QFormLayout;product=new QComboBox;warehouse=new QComboBox;unit=new QComboBox;unit->addItems({"Metre","Kg","Litre","m²","m³"});quantity=new QDoubleSpinBox;quantity->setDecimals(3);quantity->setRange(0.001,100000000);quantity->setSingleStep(0.125);quantity->setValue(1.0);reason=new QLineEdit;reason->setPlaceholderText("Örn. Fiziki sayım düzeltmesi / pilot giriş");
  QSqlQuery products(db);products.exec("SELECT id,sku || ' — ' || name FROM products ORDER BY name");while(products.next())product->addItem(products.value(1).toString(),products.value(0));
  QSqlQuery warehouses(db);warehouses.exec("SELECT id,name FROM warehouses ORDER BY name");while(warehouses.next())warehouse->addItem(warehouses.value(1).toString(),warehouses.value(0));
  form->addRow("Ürün",product);form->addRow("Depo",warehouse);form->addRow("Ölçü birimi",unit);form->addRow("Miktar (0,001)",quantity);form->addRow("Açıklama",reason);layout->addLayout(form);
  balance=new QLabel;balance->setStyleSheet("font-size:15px;font-weight:700;color:#1a344a;");layout->addWidget(balance);
  auto *actions=new QHBoxLayout;auto *enable=new QPushButton("Ürün için pilot takibi aç");auto *in=new QPushButton("+ Giriş");auto *out=new QPushButton("− Çıkış");actions->addWidget(enable);actions->addWidget(in);actions->addWidget(out);layout->addLayout(actions);
  history=new QTableWidget(0,3);history->setHorizontalHeaderLabels({"Tarih","Miktar (+/−)","Açıklama"});history->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);history->setEditTriggers(QAbstractItemView::NoEditTriggers);layout->addWidget(history,1);
  auto *close=new QPushButton("Kapat");connect(close,&QPushButton::clicked,this,&QDialog::accept);layout->addWidget(close);
  connect(product,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{updateView();});connect(warehouse,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{updateView();});
  connect(enable,&QPushButton::clicked,this,[this]{int pid=product->currentData().toInt();if(pid<=0)return;QSqlQuery q(db);q.prepare("INSERT OR IGNORE INTO fractional_stock_settings(product_id,unit) VALUES(?,?)");q.addBindValue(pid);q.addBindValue(unit->currentText());if(!q.exec())QMessageBox::warning(this,"Veritabanı",q.lastError().text());updateView();});
  auto post=[this](int sign){const qint64 milli=qRound64(quantity->value()*1000.0)*sign;QString error;if(!FractionalStock::post(db,product->currentData().toInt(),warehouse->currentData().toInt(),milli,reason->text(),&error)){QMessageBox::warning(this,"İşlem reddedildi",error);return;}updateView();};
  connect(in,&QPushButton::clicked,this,[post]{post(1);});connect(out,&QPushButton::clicked,this,[post]{post(-1);});updateView();
 }
};
