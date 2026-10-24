#pragma once
#include "FractionalStockDialog.h"
#include "FractionalUnits.h"
#include <QtWidgets>
#include <QtSql>
#include <limits>
// V3.7: separate pilot trade ledger. Not a fiscal POS sale, payment or supplier liability.
namespace FractionalTrade {
inline bool install(QSqlDatabase db,QString *error=nullptr){
 const QStringList ddl={
 "CREATE TABLE IF NOT EXISTS fractional_trade_docs(id INTEGER PRIMARY KEY,kind TEXT NOT NULL CHECK(kind IN ('Alim','Satis')),product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),quantity_milli INTEGER NOT NULL CHECK(quantity_milli>0),unit_price_cents INTEGER NOT NULL CHECK(unit_price_cents>=0),total_cents INTEGER NOT NULL CHECK(total_cents>=0),reference TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",
 "CREATE INDEX IF NOT EXISTS ix_fractional_trade_docs_product ON fractional_trade_docs(product_id,warehouse_id,id)"};
 for(const auto &sql:ddl){QSqlQuery q(db);if(!q.exec(sql)){if(error)*error=q.lastError().text();return false;}}return true;
}
inline bool post(QSqlDatabase db,const QString &kind,int productId,int warehouseId,qint64 quantityMilli,qint64 unitPriceCents,const QString &reference,QString *error=nullptr){
 auto fail=[&](const QString &s){if(error)*error=s;return false;};
 if(kind!="Alim"&&kind!="Satis")return fail("Geçersiz işlem türü.");
 if(productId<=0||warehouseId<=0||quantityMilli<=0||unitPriceCents<0||reference.trimmed().isEmpty())return fail("Zorunlu alanları doldurun.");
 // Exact HALF-UP cents rounding, overflow checked before multiplication.
 if(unitPriceCents && quantityMilli>(std::numeric_limits<qint64>::max()-500)/unitPriceCents)return fail("Tutar sınırı aşıldı.");
 const qint64 total=(quantityMilli*unitPriceCents+500)/1000;
 const qint64 delta=kind=="Alim"?quantityMilli:-quantityMilli;
 if(!db.transaction())return fail(db.lastError().text());
 auto rollback=[&](const QString &s){db.rollback();return fail(s);};
 QSqlQuery enabled(db);enabled.prepare("SELECT 1 FROM fractional_stock_settings WHERE product_id=?");enabled.addBindValue(productId);
 if(!enabled.exec()||!enabled.next())return rollback("Ürün kesirli takip için etkin değil.");
 QSqlQuery seed(db);seed.prepare("INSERT OR IGNORE INTO fractional_stock_balances(product_id,warehouse_id,quantity_milli) VALUES(?,?,0)");seed.addBindValue(productId);seed.addBindValue(warehouseId);
 if(!seed.exec())return rollback(seed.lastError().text());
 QSqlQuery stock(db);
 stock.prepare("UPDATE fractional_stock_balances SET quantity_milli=quantity_milli+? WHERE product_id=? AND warehouse_id=? AND quantity_milli+?>=0 AND ((?>0 AND quantity_milli<=9223372036854775807-?) OR ?<0)");
 for(const QVariant &v:QVariantList{delta,productId,warehouseId,delta,delta,delta,delta})stock.addBindValue(v);
 if(!stock.exec()||stock.numRowsAffected()!=1)return rollback("Yetersiz stok veya miktar sınırı.");
 QSqlQuery document(db);document.prepare("INSERT INTO fractional_trade_docs(kind,product_id,warehouse_id,quantity_milli,unit_price_cents,total_cents,reference) VALUES(?,?,?,?,?,?,?)");
 for(const QVariant &v:QVariantList{kind,productId,warehouseId,quantityMilli,unitPriceCents,total,reference.trimmed()})document.addBindValue(v);
 if(!document.exec())return rollback(document.lastError().text());
 const qint64 docId=document.lastInsertId().toLongLong();
 QSqlQuery movement(db);movement.prepare("INSERT INTO fractional_stock_movements(product_id,warehouse_id,delta_milli,reason) VALUES(?,?,?,?)");
 for(const QVariant &v:QVariantList{productId,warehouseId,delta,QString("Pilot %1 #%2: %3").arg(kind).arg(docId).arg(reference.trimmed())})movement.addBindValue(v);
 if(!movement.exec())return rollback(movement.lastError().text());
 if(!db.commit()){db.rollback();return fail(db.lastError().text());}
 return true;
}
}

class FractionalTradeDialog final:public QDialog {
 QSqlDatabase db;QComboBox *kind,*product,*warehouse,*displayUnit;QDoubleSpinBox *qty,*price;QLineEdit *reference;QLabel *total;QTableWidget *history;
 QString canonicalUnit() const {QSqlQuery q(db);q.prepare("SELECT unit FROM fractional_stock_settings WHERE product_id=?");q.addBindValue(product->currentData().toInt());return q.exec()&&q.next()?q.value(0).toString():QString();}
 void updateUnits(){const QString base=canonicalUnit();displayUnit->clear();displayUnit->addItems(FractionalUnits::choices(base));refresh();}
 void refresh(){
  const qint64 entered=qRound64(qty->value()*1000.0),cents=qRound64(price->value()*100.0);
  qint64 milli=0;QString error;
  const bool valid=FractionalUnits::convert(entered,canonicalUnit(),displayUnit->currentText(),&milli,&error);
  const qint64 amount=valid&&(!cents||milli<=(std::numeric_limits<qint64>::max()-500)/cents)?(milli*cents+500)/1000:-1;
  total->setText(amount>=0?QString("Pilot belge toplamı: %1 TL • Stok etkisi: %2 %3").arg(QString::number(amount/100.0,'f',2),FractionalStock::amount(milli),canonicalUnit()):QString("Miktar geçersiz: %1").arg(error.isEmpty()?"Tutar sınırı":error));
  QSqlQuery q(db);q.exec("SELECT d.id,d.created_at,d.kind,p.name,d.quantity_milli,d.total_cents,d.reference FROM fractional_trade_docs d JOIN products p ON p.id=d.product_id ORDER BY d.id DESC LIMIT 100");
  history->setRowCount(0);while(q.next()){int r=history->rowCount();history->insertRow(r);QStringList values={q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),FractionalStock::amount(q.value(4).toLongLong()),QString::number(q.value(5).toLongLong()/100.0,'f',2),q.value(6).toString()};for(int i=0;i<values.size();++i)history->setItem(r,i,new QTableWidgetItem(values[i]));}
 }
public:
 explicit FractionalTradeDialog(QSqlDatabase database,QWidget *parent=nullptr):QDialog(parent),db(database){
  setWindowTitle("KasaPro | Kesirli Pilot Alım / Satım");resize(1040,720);setMinimumSize(760,540);
  auto *outer=new QVBoxLayout(this);outer->setContentsMargins(20,20,20,20);outer->setSpacing(12);
  auto *heading=new QLabel("Kesirli Alım / Satım — Pilot Defter");heading->setStyleSheet("font-size:20px;font-weight:700;");outer->addWidget(heading);
  auto *notice=new QLabel("DİKKAT: Birim fiyatı ANA STOK BİRİMİ başınadır (ör. TL/kg). Pilot belge yalnızca bağımsız kesirli stok defterini etkiler. Gerçek POS satışı, ödeme, mali fiş, satın alma borcu ve cari hesap OLUŞTURMAZ. Canlı işlem için kullanmayın.");notice->setWordWrap(true);notice->setStyleSheet("background:#fff4df;color:#714200;padding:12px;");outer->addWidget(notice);
  auto *form=new QFormLayout;kind=new QComboBox;kind->addItems({"Alim","Satis"});product=new QComboBox;warehouse=new QComboBox;displayUnit=new QComboBox;qty=new QDoubleSpinBox;qty->setDecimals(3);qty->setRange(.001,1000000);qty->setValue(1);price=new QDoubleSpinBox;price->setDecimals(2);price->setRange(0,100000000);reference=new QLineEdit;reference->setPlaceholderText("Belge veya test açıklaması");
  QSqlQuery products(db);products.exec("SELECT p.id,p.sku || ' — ' || p.name || ' (' || fs.unit || ')' FROM fractional_stock_settings fs JOIN products p ON p.id=fs.product_id ORDER BY p.name");while(products.next())product->addItem(products.value(1).toString(),products.value(0));
  QSqlQuery warehouses(db);warehouses.exec("SELECT id,name FROM warehouses ORDER BY name");while(warehouses.next())warehouse->addItem(warehouses.value(1).toString(),warehouses.value(0));
  form->addRow("İşlem",kind);form->addRow("Kesirli ürün",product);form->addRow("Depo",warehouse);form->addRow("Miktar",qty);form->addRow("Girilen birim",displayUnit);form->addRow("Birim fiyatı (TL)",price);form->addRow("Referans",reference);outer->addLayout(form);
  total=new QLabel;total->setStyleSheet("font-size:16px;font-weight:700;");outer->addWidget(total);
  auto *buttons=new QHBoxLayout;auto *save=new QPushButton("Pilot belgeyi kaydet");auto *close=new QPushButton("Kapat");buttons->addWidget(save);buttons->addStretch();buttons->addWidget(close);outer->addLayout(buttons);
  history=new QTableWidget(0,7);history->setHorizontalHeaderLabels({"No","Tarih","Tür","Ürün","Miktar","Toplam TL","Referans"});history->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);history->setSelectionBehavior(QAbstractItemView::SelectRows);history->setEditTriggers(QAbstractItemView::NoEditTriggers);outer->addWidget(history,1);
  connect(product,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{updateUnits();});connect(displayUnit,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{refresh();});connect(qty,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this]{refresh();});connect(price,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this]{refresh();});connect(close,&QPushButton::clicked,this,&QDialog::accept);
  connect(save,&QPushButton::clicked,this,[this]{QString err;qint64 baseMilli=0;if(!FractionalUnits::convert(qRound64(qty->value()*1000.0),canonicalUnit(),displayUnit->currentText(),&baseMilli,&err)){QMessageBox::warning(this,"Miktar hatası",err);return;}if(!FractionalTrade::post(db,kind->currentText(),product->currentData().toInt(),warehouse->currentData().toInt(),baseMilli,qRound64(price->value()*100),reference->text(),&err)){QMessageBox::warning(this,"İşlem engellendi",err);return;}reference->clear();refresh();QMessageBox::information(this,"Pilot kayıt","Kesirli stok ve pilot belge kaydedildi. Mali/ticari satış değildir.");});updateUnits();
 }
};
