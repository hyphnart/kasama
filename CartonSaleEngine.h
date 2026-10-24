#pragma once
#include <QtSql>
#include <QString>
#include <QVariantList>
#include <climits>
// Invoke inside the caller's existing BEGIN/COMMIT transaction, BEFORE reducing stock.qty.
// Legacy products without physical carton rows retain the legacy sale path.
namespace CartonSaleEngine {
inline bool exec(QSqlDatabase db,const QString &sql,const QVariantList &args,QString *error){
 QSqlQuery q(db);q.prepare(sql);for(const auto &a:args)q.addBindValue(a);
 if(q.exec())return true;if(error)*error=q.lastError().text();return false;
}
inline bool install(QSqlDatabase db,QString *error=nullptr){
 return exec(db,"CREATE TABLE IF NOT EXISTS carton_sale_events(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL,warehouse_id INTEGER NOT NULL,pack_id INTEGER NOT NULL,sale_doc_id INTEGER NOT NULL,opened_cases INTEGER NOT NULL,units_per_pack_snapshot INTEGER NOT NULL,sold_units INTEGER NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",{},error);
}
inline bool consume(QSqlDatabase db,int productId,int warehouseId,qint64 sold,int docId,QString *error){
 if(sold<=0||docId<=0){if(error)*error="Geçersiz miktar veya satış belgesi.";return false;}
 QSqlQuery check(db);
 check.prepare("SELECT p.id,p.units_per_pack,c.sealed_qty,c.loose_units,COALESCE(pol.opening_policy,'manual') "
 "FROM carton_stock c JOIN product_packs p ON p.id=c.pack_id "
 "LEFT JOIN carton_policies pol ON pol.pack_id=p.id "
 "WHERE p.product_id=? AND c.warehouse_id=? AND (c.sealed_qty>0 OR c.loose_units>0)");
 check.addBindValue(productId);check.addBindValue(warehouseId);
 if(!check.exec()){if(error)*error=check.lastError().text();return false;}
 int rows=0,packId=0; qint64 per=0,sealed=0,loose=0;QString policy;
 while(check.next()){
  ++rows;if(rows>1){if(error)*error="Birden fazla aktif ambalaj defteri var. Önce mutabakat yapın.";return false;}
  packId=check.value(0).toInt();per=check.value(1).toLongLong();sealed=check.value(2).toLongLong();loose=check.value(3).toLongLong();policy=check.value(4).toString();
 }
 if(rows==0)return true;
 QSqlQuery stock(db);stock.prepare("SELECT qty,reserved FROM stock WHERE product_id=? AND warehouse_id=?");
 stock.addBindValue(productId);stock.addBindValue(warehouseId);
 if(!stock.exec()||!stock.next()){if(error)*error="Stok kaydı bulunamadı.";return false;}
 const qint64 qty=stock.value(0).toLongLong(),reserved=stock.value(1).toLongLong();
 if(per<=0||sealed<0||loose<0||sealed>(LLONG_MAX-loose)/per||sealed*per+loose!=qty){if(error)*error="Ambalaj ve ana stok sayıları eşleşmiyor. Önce mutabakat yapın.";return false;}
 if(qty-reserved<sold){if(error)*error="Satılabilir stok yetersiz.";return false;}
 const qint64 shortage=qMax(qint64(0),sold-loose);
 const qint64 opened=shortage==0?0:1+(shortage-1)/per;
 if(opened>sealed){if(error)*error="Kapalı koli sayısı yetersiz.";return false;}
 if(opened>0&&policy!="manual"){
  if(error)*error=policy=="forbid"?"Bu ürünün kolisi açılamaz.":"Bu ambalaj yönetici onayı gerektiriyor; yetkili işlem yapılmadan satış tamamlanamaz.";
  return false;
 }
 // Guarded, atomic balance update. An UPDATE affecting zero rows is a failure.
 if(!exec(db,"UPDATE carton_stock SET sealed_qty=sealed_qty-?,loose_units=loose_units+?-?,updated_at=CURRENT_TIMESTAMP WHERE warehouse_id=? AND pack_id=? AND sealed_qty>=? AND loose_units=?",{opened,opened*per,sold,warehouseId,packId,opened,loose},error))return false;
 QSqlQuery changes(db);if(!changes.exec("SELECT changes()")||!changes.next()||changes.value(0).toInt()!=1){if(error)*error="Koli stok kaydı güncellenemedi.";return false;}
 return exec(db,"INSERT INTO carton_sale_events(product_id,warehouse_id,pack_id,sale_doc_id,opened_cases,units_per_pack_snapshot,sold_units) VALUES(?,?,?,?,?,?,?)",{productId,warehouseId,packId,docId,opened,per,sold},error);
}
}
