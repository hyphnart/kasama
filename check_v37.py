from pathlib import Path
import sqlite3
p=Path(__file__).parent
h=(p/'FractionalTradeDialog.h').read_text();m=(p/'main.cpp').read_text()
for x in ['fractional_trade_docs','db.transaction()','db.rollback()','quantity_milli+?>=0','unit_price_cents','FractionalTrade::post','reference.trimmed()']:
 assert x in h,x
assert 'FractionalTrade::install(db' in m
assert 'Kesirli Pilot Alım / Satım' in m
assert 'project(KasaPro VERSION ' in (p/'CMakeLists.txt').read_text()
conn=sqlite3.connect(':memory:');conn.execute('PRAGMA foreign_keys=ON')
conn.executescript('''
 CREATE TABLE products(id INTEGER PRIMARY KEY); CREATE TABLE warehouses(id INTEGER PRIMARY KEY);
 INSERT INTO products VALUES(1); INSERT INTO warehouses VALUES(1);
 CREATE TABLE fractional_stock_settings(product_id INTEGER PRIMARY KEY,unit TEXT NOT NULL);
 INSERT INTO fractional_stock_settings VALUES(1,'Metre');
 CREATE TABLE fractional_stock_balances(product_id INTEGER,warehouse_id INTEGER,quantity_milli INTEGER NOT NULL CHECK(quantity_milli>=0),PRIMARY KEY(product_id,warehouse_id));
 CREATE TABLE fractional_stock_movements(id INTEGER PRIMARY KEY,product_id INTEGER,warehouse_id INTEGER,delta_milli INTEGER NOT NULL,reason TEXT NOT NULL);
 CREATE TABLE fractional_trade_docs(id INTEGER PRIMARY KEY,kind TEXT NOT NULL CHECK(kind IN ('Alim','Satis')),product_id INTEGER,warehouse_id INTEGER,quantity_milli INTEGER NOT NULL CHECK(quantity_milli>0),unit_price_cents INTEGER NOT NULL CHECK(unit_price_cents>=0),total_cents INTEGER NOT NULL CHECK(total_cents>=0),reference TEXT NOT NULL);
 INSERT INTO fractional_stock_balances VALUES(1,1,1750);
''')
def trade(kind,amount,price):
 conn.execute('BEGIN')
 try:
  delta=amount if kind=='Alim' else -amount
  updated=conn.execute('UPDATE fractional_stock_balances SET quantity_milli=quantity_milli+? WHERE product_id=1 AND warehouse_id=1 AND quantity_milli+?>=0',(delta,delta))
  if updated.rowcount!=1:raise ValueError('insufficient stock')
  conn.execute('INSERT INTO fractional_trade_docs(kind,product_id,warehouse_id,quantity_milli,unit_price_cents,total_cents,reference) VALUES(?,1,1,?,?,?,?)',(kind,amount,price,(amount*price+500)//1000,'test'))
  conn.execute('INSERT INTO fractional_stock_movements(product_id,warehouse_id,delta_milli,reason) VALUES(1,1,?,?)',(delta,'test'))
  conn.commit()
 except Exception:
  conn.rollback();raise
trade('Alim',2250,1250)
trade('Satis',1750,1250)
assert conn.execute('SELECT quantity_milli FROM fractional_stock_balances').fetchone()[0]==2250
assert conn.execute('SELECT total_cents FROM fractional_trade_docs WHERE kind="Satis"').fetchone()[0]==2188
try:trade('Satis',3000,100)
except ValueError:pass
else:raise AssertionError('Oversell accepted')
assert conn.execute('SELECT COUNT(*) FROM fractional_trade_docs').fetchone()[0]==2
assert conn.execute('SELECT quantity_milli FROM fractional_stock_balances').fetchone()[0]==2250
print('PASS: v37 integration markers, half-up rounding, atomic simulated inventory operations')
