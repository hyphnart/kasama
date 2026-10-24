#pragma once
#include "FractionalStockDialog.h"
#include <QtWidgets>
#include <QtSql>
#include <QFile>
#include <QTextStream>

// Read-only reconciliation. Deliberately does not move or merge any inventory.
class FractionalReconciliationDialog final : public QDialog {
 QSqlDatabase database;
 QTableWidget *grid=nullptr;
 QLabel *summary=nullptr;
 struct Entry {QStringList cells; bool alert=false;};
 QVector<Entry> entries;
 void load(){
  entries.clear();
  // Compare ledger balances to actual movement sum; spot legacy stock overlap per warehouse.
  QSqlQuery q(database);
  const QString sql=QStringLiteral(R"SQL(
   SELECT p.sku,p.name,w.name,fs.unit,
          COALESCE(b.quantity_milli,0),
          COALESCE((SELECT SUM(m.delta_milli) FROM fractional_stock_movements m
                    WHERE m.product_id=fs.product_id AND m.warehouse_id=w.id),0),
          COALESCE(s.qty,0),COALESCE(s.reserved,0),
          COALESCE((SELECT COUNT(*) FROM fractional_trade_docs d
                    WHERE d.product_id=fs.product_id AND d.warehouse_id=w.id),0)
   FROM fractional_stock_settings fs
   JOIN products p ON p.id=fs.product_id
   CROSS JOIN warehouses w
   LEFT JOIN fractional_stock_balances b ON b.product_id=fs.product_id AND b.warehouse_id=w.id
   LEFT JOIN stock s ON s.product_id=fs.product_id AND s.warehouse_id=w.id
   WHERE COALESCE(b.quantity_milli,0)<>0 OR COALESCE(s.qty,0)<>0 OR COALESCE(s.reserved,0)<>0
       OR EXISTS(SELECT 1 FROM fractional_stock_movements m WHERE m.product_id=fs.product_id AND m.warehouse_id=w.id)
   ORDER BY p.name,w.name
  )SQL");
  if(!q.exec(sql)){QMessageBox::warning(this,"Mutabakat sorgusu",q.lastError().text());return;}
  int risky=0, mismatch=0;
  while(q.next()){
   const qint64 balance=q.value(4).toLongLong(), movements=q.value(5).toLongLong();
   const qint64 oldStock=q.value(6).toLongLong(), reserved=q.value(7).toLongLong();
   const bool both=oldStock>0 && (balance!=0 || movements!=0);
   const bool different=balance!=movements;
   const bool alert=both||different||reserved>0;
   if(alert)++risky;if(different)++mismatch;
   QString status;
   if(different) status="Hareket/bakiye farkı";
   else if(both) status="İki defterde stok";
   else if(reserved>0) status="Eski stok rezervasyonu";
   else status="Kontrol edildi (pilot)";
   entries.append({{q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),FractionalStock::amount(balance),FractionalStock::amount(movements),QString::number(oldStock),QString::number(reserved),q.value(8).toString(),status},alert});
  }
  grid->setRowCount(entries.size());
  for(int i=0;i<entries.size();++i){
   const Entry &e=entries.at(i);
   for(int c=0;c<e.cells.size();++c){
    auto *cell=new QTableWidgetItem(e.cells.at(c));
    if(e.alert)cell->setBackground(QColor("#FFF0E5"));
    grid->setItem(i,c,cell);
   }
  }
  summary->setText(QString("Kontrol edilen ürün/depo: %1   |   Uyarı: %2   |   Hareket farkı: %3")
      .arg(entries.size()).arg(risky).arg(mismatch));
 }
 void exportCsv(){
  const QString path=QFileDialog::getSaveFileName(this,"Mutabakat CSV","kasapro-kesirli-mutabakat.csv","CSV (*.csv)");
  if(path.isEmpty())return;
  QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate|QIODevice::Text)){
   QMessageBox::warning(this,"Dosya",f.errorString());return;
  }
  QTextStream out(&f);out.setEncoding(QStringConverter::Utf8);
  out<<QChar(0xFEFF);
  auto quoted=[](QString s){s.replace('"',"\"\"");return '"'+s+'"';};
  QStringList headers={"Stok Kodu","Ürün","Depo","Birim","Kesirli Bakiye","Hareket Toplamı","Eski Stok","Rezerve","Pilot Belge","Durum"};
  for(auto &s:headers)s=quoted(s);out<<headers.join(';')<<'\n';
  for(const Entry &e:entries){QStringList fields=e.cells;for(auto &s:fields)s=quoted(s);out<<fields.join(';')<<'\n';}
  f.close();if(f.error()!=QFileDevice::NoError)QMessageBox::warning(this,"Dosya",f.errorString());
 }
public:
 explicit FractionalReconciliationDialog(QSqlDatabase db,QWidget *parent=nullptr):QDialog(parent),database(db){
  setWindowTitle("KasaPro | Kesirli Stok Mutabakatı (Salt Okunur)");resize(1170,730);setMinimumSize(770,480);
  auto *layout=new QVBoxLayout(this);layout->setContentsMargins(20,20,20,20);layout->setSpacing(12);
  auto *heading=new QLabel("Kesirli Stok Mutabakat ve Risk Denetimi");heading->setStyleSheet("font-size:20px;font-weight:700;");layout->addWidget(heading);
  auto *note=new QLabel("Salt okunur denetimdir. Pilot kesirli defter ile gerçek POS stokları birleştirilmez; hiçbir bakiye veya satış kaydı değiştirilmez. 'Kontrol edildi' sonucu canlı POS entegrasyon onayı değildir.");
  note->setWordWrap(true);note->setStyleSheet("background:#fff4df;color:#714200;padding:12px;");layout->addWidget(note);
  summary=new QLabel;layout->addWidget(summary);
  grid=new QTableWidget(0,10,this);
  grid->setHorizontalHeaderLabels({"Stok Kodu","Ürün","Depo","Birim","Kesirli Bakiye","Hareket Toplamı","Eski Stok","Rezerve","Pilot Belge","Durum"});
  grid->setSelectionBehavior(QAbstractItemView::SelectRows);grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
  grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);grid->horizontalHeader()->setStretchLastSection(true);
  grid->verticalHeader()->setVisible(false);grid->setAlternatingRowColors(true);layout->addWidget(grid,1);
  auto *actions=new QHBoxLayout;auto *refresh=new QPushButton("Yenile");auto *exportButton=new QPushButton("CSV Dışa Aktar");auto *close=new QPushButton("Kapat");
  actions->addWidget(refresh);actions->addWidget(exportButton);actions->addStretch();actions->addWidget(close);layout->addLayout(actions);
  connect(refresh,&QPushButton::clicked,this,[this]{load();});connect(exportButton,&QPushButton::clicked,this,[this]{exportCsv();});connect(close,&QPushButton::clicked,this,&QDialog::accept);load();
 }
};
