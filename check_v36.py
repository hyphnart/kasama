from pathlib import Path
import sqlite3
p=Path(__file__).parent
h=(p/'FractionalStockDialog.h').read_text();m=(p/'main.cpp').read_text()
for token in ['quantity_milli INTEGER NOT NULL DEFAULT 0 CHECK(quantity_milli>=0)','db.transaction()','db.rollback()','numRowsAffected()!=1','fractional_stock_movements','FractionalStock::post']:
 assert token in h,token
assert 'FractionalStock::install(db' in m
assert 'Kesirli Stok Defteri' in m
c=sqlite3.connect(':memory:')
c.executescript('CREATE TABLE products(id INTEGER PRIMARY KEY);CREATE TABLE warehouses(id INTEGER PRIMARY KEY);\nINSERT INTO products VALUES(1);INSERT INTO warehouses VALUES(1);\nCREATE TABLE fractional_stock_balances(product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),quantity_milli INTEGER NOT NULL DEFAULT 0 CHECK(quantity_milli>=0),PRIMARY KEY(product_id,warehouse_id));\nINSERT INTO fractional_stock_balances VALUES(1,1,1750);')
try:
 c.execute('UPDATE fractional_stock_balances SET quantity_milli=quantity_milli-2000 WHERE product_id=1')
 raise AssertionError('negative quantity accepted')
except sqlite3.IntegrityError:pass
assert c.execute('SELECT quantity_milli FROM fractional_stock_balances').fetchone()[0]==1750
print('PASS: v36 source checks, SQLite nonnegative invariant')
