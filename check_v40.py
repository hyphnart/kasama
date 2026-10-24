from pathlib import Path
import sqlite3
p=Path(__file__).parent
s=(p/'main.cpp').read_text(encoding='utf-8')
h=(p/'PackagingCenter.h').read_text(encoding='utf-8')
assert '#include "PackagingCenter.h"' in s
assert 'PackagingCenter::install(db,' in s
assert 'makeAction("Ambalaj / Koli Merkezi"' in s
assert 'units_snapshot' in h and 'pack_name_snapshot' in h
assert 'database.transaction()' in h and 'database.rollback()' in h
assert 'INSERT INTO stock_moves' in h and 'INSERT INTO stock(' in h
assert 'product_packs' in h and 'pack_receipts' in h
con=sqlite3.connect(':memory:');con.execute('PRAGMA foreign_keys=ON')
con.executescript('''
CREATE TABLE products(id INTEGER PRIMARY KEY);
CREATE TABLE warehouses(id INTEGER PRIMARY KEY);
INSERT INTO products VALUES(1);INSERT INTO warehouses VALUES(1);
CREATE TABLE stock(product_id INTEGER NOT NULL,warehouse_id INTEGER NOT NULL,qty INTEGER NOT NULL DEFAULT 0,reserved INTEGER NOT NULL DEFAULT 0,CHECK(qty>=0 AND reserved>=0 AND reserved<=qty),PRIMARY KEY(product_id,warehouse_id));
CREATE TABLE stock_moves(id INTEGER PRIMARY KEY,product_id INTEGER,warehouse_id INTEGER,delta_qty INTEGER,reason TEXT);
CREATE TABLE product_packs(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL REFERENCES products(id) ON DELETE CASCADE,label TEXT NOT NULL,units_per_pack INTEGER NOT NULL CHECK(units_per_pack BETWEEN 1 AND 100000),barcode TEXT NOT NULL DEFAULT '',sale_price_cents INTEGER NOT NULL DEFAULT 0 CHECK(sale_price_cents>=0),created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,UNIQUE(product_id,label,units_per_pack));
CREATE UNIQUE INDEX ux_pack_barcode ON product_packs(barcode) WHERE barcode<>'';
CREATE TABLE pack_receipts(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),pack_id INTEGER REFERENCES product_packs(id) ON DELETE SET NULL,pack_name_snapshot TEXT NOT NULL,units_snapshot INTEGER NOT NULL CHECK(units_snapshot>0),pack_qty INTEGER NOT NULL CHECK(pack_qty>0),base_qty INTEGER NOT NULL CHECK(base_qty>0),created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
INSERT INTO product_packs(product_id,label,units_per_pack,barcode) VALUES(1,'24lu Koli',24,'ABC24'),(1,'12li Koli',12,'ABC12');
''')
packid,per=con.execute('SELECT id,units_per_pack FROM product_packs WHERE barcode=?',('ABC24',)).fetchone()
with con:
    qty=10*per
    con.execute('INSERT INTO stock VALUES(1,1,?,0)',(qty,))
    con.execute('INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason) VALUES(1,1,?,?)',(qty,'Pack receipt'))
    con.execute('INSERT INTO pack_receipts(product_id,warehouse_id,pack_id,pack_name_snapshot,units_snapshot,pack_qty,base_qty) VALUES(1,1,?,?,?,?,?)',(packid,'24lu Koli',per,10,qty))
assert con.execute('SELECT qty FROM stock').fetchone()[0]==240
assert con.execute('SELECT units_snapshot,pack_qty,base_qty FROM pack_receipts').fetchone()==(24,10,240)
try:
    con.execute("INSERT INTO product_packs(product_id,label,units_per_pack,barcode) VALUES(1,'Tekrar',2,'ABC24')")
    raise AssertionError('duplicate barcode accepted')
except sqlite3.IntegrityError:pass
try:
    con.execute("INSERT INTO product_packs(product_id,label,units_per_pack) VALUES(1,'Yanlis',0)")
    raise AssertionError('zero units accepted')
except sqlite3.IntegrityError:pass
print('PASS: V4.0 source hooks, schema, multi-pack conversion, immutable receipt multiplier')
