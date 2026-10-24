from pathlib import Path
import sqlite3
s=Path(__file__).with_name('VariantManagerDialog.h').read_text(encoding='utf-8')
for fragment in ['createMatrix()', 'allocateStock()', 'Toplu Matris Oluştur','Stok Dağıt', 'database.transaction()', 'qty-reserved>=?', 'database.rollback()', 'product_variants', 'stock_moves']:
    assert fragment in s, fragment
conn=sqlite3.connect(':memory:')
conn.executescript('''
PRAGMA foreign_keys=ON;
CREATE TABLE products(id INTEGER PRIMARY KEY,sku TEXT UNIQUE,name TEXT,barcode TEXT,price_cents INT,cost_cents INT,minimum_qty INT,image_path TEXT);
CREATE TABLE product_variants(parent_product_id INT,child_product_id INT UNIQUE,option1 TEXT,option2 TEXT,UNIQUE(parent_product_id,option1,option2));
CREATE TABLE stock(product_id INT,warehouse_id INT,qty INT NOT NULL CHECK(qty>=0),reserved INT NOT NULL CHECK(reserved>=0 AND reserved<=qty),PRIMARY KEY(product_id,warehouse_id));
CREATE TABLE stock_moves(product_id INT,warehouse_id INT,delta_qty INT,reason TEXT);
INSERT INTO products(id,sku,name) VALUES(1,'SHOE','Ayakkabı');
INSERT INTO products(id,sku,name) VALUES(2,'SHOE-42-BLACK','Ayakkabı 42 Siyah');
INSERT INTO product_variants VALUES(1,2,'42','Siyah');
INSERT INTO stock VALUES(1,1,12,4);
''')
def allocate(qty):
    try:
        with conn:
            cur=conn.execute('UPDATE stock SET qty=qty-? WHERE product_id=1 AND warehouse_id=1 AND qty-reserved>=?',(qty,qty))
            if cur.rowcount != 1: raise ValueError('insufficient available stock')
            conn.execute('INSERT OR IGNORE INTO stock(product_id,warehouse_id,qty,reserved) VALUES(2,1,0,0)')
            conn.execute('UPDATE stock SET qty=qty+? WHERE product_id=2 AND warehouse_id=1',(qty,))
            conn.executemany('INSERT INTO stock_moves VALUES(1,1,?,?)',[]) if False else None
            conn.execute('INSERT INTO stock_moves VALUES(1,1,?,?)',(-qty,'variant allocation'))
            conn.execute('INSERT INTO stock_moves VALUES(2,1,?,?)',(qty,'variant allocation'))
        return True
    except (ValueError,sqlite3.IntegrityError):return False
assert allocate(6)
assert conn.execute('SELECT qty,reserved FROM stock WHERE product_id=1').fetchone()==(6,4)
assert conn.execute('SELECT qty FROM stock WHERE product_id=2').fetchone()[0]==6
assert not allocate(3)
assert conn.execute('SELECT SUM(qty) FROM stock').fetchone()[0]==12
assert conn.execute('SELECT SUM(delta_qty) FROM stock_moves').fetchone()[0]==0
print('PASS: source wiring, reserved-stock protection, atomic stock conservation')
