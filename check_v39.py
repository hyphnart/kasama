from pathlib import Path
import re
_version=re.search(r'project\(KasaPro VERSION (\d+)\.(\d+)',(Path(__file__).parent/'CMakeLists.txt').read_text())
assert _version and (int(_version.group(1)),int(_version.group(2))) >= (4,1)
import sqlite3
p=Path(__file__).parent
h=(p/'FractionalReconciliationDialog.h').read_text()
m=(p/'main.cpp').read_text()
assert '#include "FractionalReconciliationDialog.h"' in m
assert 'makeAction("Kesirli Stok Mutabakatı"' in m
assert '"Kesirli Pilot Alım / Satım","Kesirli Stok Mutabakatı"' in m
assert 'QFileDialog::getSaveFileName' in h
assert 'FROM fractional_stock_settings fs' in h
assert 'LEFT JOIN stock s' in h
assert 'balance!=movements' in h
assert 'Salt okunur' in h
assert 'INSERT INTO' not in h and 'UPDATE ' not in h and 'DELETE FROM' not in h
# Independently execute report SQL against a representative SQLite schema
sql=h.split('R"SQL(',1)[1].split(')SQL"',1)[0]
con=sqlite3.connect(':memory:')
con.executescript("CREATE TABLE products(id INTEGER PRIMARY KEY,sku TEXT,name TEXT);CREATE TABLE warehouses(id INTEGER PRIMARY KEY,name TEXT);CREATE TABLE fractional_stock_settings(product_id INTEGER,unit TEXT);CREATE TABLE fractional_stock_balances(product_id INTEGER,warehouse_id INTEGER,quantity_milli INTEGER);CREATE TABLE fractional_stock_movements(product_id INTEGER,warehouse_id INTEGER,delta_milli INTEGER);CREATE TABLE stock(product_id INTEGER,warehouse_id INTEGER,qty INTEGER,reserved INTEGER);CREATE TABLE fractional_trade_docs(product_id INTEGER,warehouse_id INTEGER);INSERT INTO products VALUES(1,'SKU','Kablo');INSERT INTO warehouses VALUES(1,'Ana');INSERT INTO fractional_stock_settings VALUES(1,'Metre');INSERT INTO fractional_stock_balances VALUES(1,1,5000);INSERT INTO fractional_stock_movements VALUES(1,1,2000);INSERT INTO stock VALUES(1,1,4,0);")
rows=con.execute(sql).fetchall();assert len(rows)==1
r=rows[0];assert r[4]==5000 and r[5]==2000 and r[6]==4
assert r[4]!=r[5] and r[6]>0
print('PASS: V3.9 read-only reconciliation SQL and integration')
