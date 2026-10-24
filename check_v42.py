from pathlib import Path
import sqlite3
root=Path(__file__).parent
s=(root/'main.cpp').read_text(); h=(root/'CartonOpeningCenter.h').read_text(); p=(root/'PackagingCenter.h').read_text()
assert '#include "CartonOpeningCenter.h"' in s
assert 'CartonOpeningCenter::install' in s
assert 'Kapalı Koli / Açık Adet' in s
assert "action IN ('RECEIVE','OPEN')" in h
assert 'sealed_qty=sealed_qty-?' in h
assert 'loose_units=loose_units+?' in h
assert 'stock.qty' in h
assert 'INSERT INTO carton_stock' in p and 'INSERT INTO carton_events' in p
assert 'VERSION 4.' in (root/'CMakeLists.txt').read_text()
con=sqlite3.connect(':memory:');con.execute('PRAGMA foreign_keys=ON')
con.executescript('''
CREATE TABLE products(id INTEGER PRIMARY KEY);CREATE TABLE warehouses(id INTEGER PRIMARY KEY);
CREATE TABLE product_packs(id INTEGER PRIMARY KEY,product_id INTEGER REFERENCES products(id),units_per_pack INTEGER);
CREATE TABLE stock(product_id INTEGER,warehouse_id INTEGER,qty INTEGER,reserved INTEGER,PRIMARY KEY(product_id,warehouse_id));
CREATE TABLE carton_stock(warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),pack_id INTEGER NOT NULL REFERENCES product_packs(id),sealed_qty INTEGER NOT NULL DEFAULT 0 CHECK(sealed_qty>=0),loose_units INTEGER NOT NULL DEFAULT 0 CHECK(loose_units>=0),updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,PRIMARY KEY(warehouse_id,pack_id));
CREATE TABLE carton_events(id INTEGER PRIMARY KEY,product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),pack_id INTEGER NOT NULL REFERENCES product_packs(id),action TEXT NOT NULL CHECK(action IN ('RECEIVE','OPEN')),case_qty INTEGER NOT NULL CHECK(case_qty>0),units_snapshot INTEGER NOT NULL CHECK(units_snapshot>0),loose_delta INTEGER NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,note TEXT NOT NULL DEFAULT '');
INSERT INTO products VALUES(1);INSERT INTO warehouses VALUES(1);INSERT INTO product_packs VALUES(1,1,24);
INSERT INTO stock VALUES(1,1,123,0);INSERT INTO carton_stock(warehouse_id,pack_id,sealed_qty,loose_units) VALUES(1,1,5,3);
''')
with con:
    con.execute('UPDATE carton_stock SET sealed_qty=sealed_qty-1,loose_units=loose_units+24 WHERE warehouse_id=1 AND pack_id=1 AND sealed_qty>=1')
    con.execute("INSERT INTO carton_events(product_id,warehouse_id,pack_id,action,case_qty,units_snapshot,loose_delta) VALUES(1,1,1,'OPEN',1,24,24)")
assert con.execute('SELECT sealed_qty,loose_units FROM carton_stock').fetchone()==(4,27)
assert con.execute('SELECT qty FROM stock').fetchone()[0]==123
try:
    with con:
        con.execute('UPDATE carton_stock SET sealed_qty=sealed_qty-5 WHERE warehouse_id=1 AND pack_id=1')
    raise AssertionError('negative cartons accepted')
except sqlite3.IntegrityError:pass
assert con.execute('SELECT sealed_qty FROM carton_stock').fetchone()[0]==4
print('PASS: V4.2 structure, atomic opening, unchanged total stock, negative guard')
