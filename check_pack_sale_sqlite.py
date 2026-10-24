import sqlite3
c=sqlite3.connect(':memory:')
c.executescript('''
CREATE TABLE products(id INTEGER PRIMARY KEY,name TEXT,price_cents INTEGER);
CREATE TABLE warehouses(id INTEGER PRIMARY KEY,name TEXT);
CREATE TABLE stock(product_id INTEGER,warehouse_id INTEGER,qty INTEGER,reserved INTEGER DEFAULT 0,PRIMARY KEY(product_id,warehouse_id));
CREATE TABLE product_packs(id INTEGER PRIMARY KEY,product_id INTEGER,label TEXT,units_per_pack INTEGER,barcode TEXT,sale_price_cents INTEGER);
CREATE TABLE docs(id INTEGER PRIMARY KEY,total_cents INTEGER);
CREATE TABLE doc_lines(doc_id INTEGER,product_id INTEGER,warehouse_id INTEGER,qty INTEGER,unit_cents INTEGER);
INSERT INTO products VALUES(1,'Sabun',2500);
INSERT INTO warehouses VALUES(1,'Ana');
INSERT INTO stock VALUES(1,1,240,24);
INSERT INTO product_packs VALUES(1,1,'24lu koli',24,'KOLI24',48000);
''')
p=c.execute('SELECT units_per_pack,sale_price_cents FROM product_packs WHERE barcode=?',('KOLI24',)).fetchone()
assert p==(24,48000)
assert p[1]%p[0]==0
qty=p[0]; unit=p[1]//p[0]
with c:
    result=c.execute('UPDATE stock SET qty=qty-? WHERE product_id=1 AND warehouse_id=1 AND qty-reserved>=?',(qty,qty))
    assert result.rowcount==1
    c.execute('INSERT INTO docs(total_cents) VALUES(?)',(qty*unit,))
    c.execute('INSERT INTO doc_lines VALUES(1,1,1,?,?)',(qty,unit))
assert c.execute('SELECT qty,reserved FROM stock').fetchone()==(216,24)
assert c.execute('SELECT SUM(qty*unit_cents) FROM doc_lines').fetchone()[0]==48000
assert 47999%24 != 0 # unsupported exact per-unit cents
# No change when insufficient stock
with c:
    cur=c.execute('UPDATE stock SET qty=qty-? WHERE product_id=1 AND warehouse_id=1 AND qty-reserved>=?',(1000,1000))
    assert cur.rowcount==0
assert c.execute('SELECT qty FROM stock').fetchone()[0]==216
print('PASS transactional POS sale, 24 pack base units, price exactness and low-stock rejection')
