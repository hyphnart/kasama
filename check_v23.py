from pathlib import Path
import re,sqlite3,tempfile
src=Path(__file__).with_name('main.cpp').read_text(encoding='utf8')
checks={
 'single main':len(re.findall(r'\bint\s+main\s*\(',src))==1,
 'returns migration':'CREATE TABLE IF NOT EXISTS sale_returns' in src,
 'return lines migration':'CREATE TABLE IF NOT EXISTS sale_return_lines' in src,
 'safe payment restriction':'methods!=1' in src and 'payMethod!="Nakit"' in src,
 'refund stock atomic':'INSERT INTO sale_return_lines' in src and 'if(!db.transaction())' in src,
 'refund audit':'INSERT INTO stock_moves' in src and 'Satis Iadesi' in src,
 'UI integration':'Satışlar / İadeler' in src and 'salesReturnCenter(this)' in src,
 'existing payment device center':'DeviceConnectionCenter' in src,
 'existing stock purchase flow':'Mal kabul' in src,
 'no differing auto declaration':all(len(set(re.findall(r'new\s+(\w+)',m.group(0))))==1 for m in re.finditer(r'auto\s*\*\s*\w+\s*=\s*new\s+\w+(?:\s*,\s*\*\s*\w+\s*=\s*new\s+\w+)+',src)),
}
for k,v in checks.items():print(('PASS' if v else 'FAIL')+': '+k)
assert all(checks.values())
# Transaction / schema invariant smoke test: representative known structure, rollback and partial cap.
with tempfile.TemporaryDirectory() as d:
 con=sqlite3.connect(d+'/test.sqlite');con.execute('PRAGMA foreign_keys=ON')
 con.executescript('''CREATE TABLE docs(id INTEGER PRIMARY KEY,total_cents INTEGER); CREATE TABLE products(id INTEGER PRIMARY KEY);CREATE TABLE warehouses(id INTEGER PRIMARY KEY);CREATE TABLE doc_lines(id INTEGER PRIMARY KEY,doc_id INTEGER REFERENCES docs(id));CREATE TABLE sale_returns(id INTEGER PRIMARY KEY,sale_doc_id INTEGER NOT NULL REFERENCES docs(id),party_id INTEGER,refund_method TEXT NOT NULL CHECK(refund_method IN ('Nakit','Veresiye')),refund_cents INTEGER NOT NULL CHECK(refund_cents>0),reason TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);CREATE TABLE sale_return_lines(id INTEGER PRIMARY KEY,return_id INTEGER NOT NULL REFERENCES sale_returns(id),sale_line_id INTEGER NOT NULL REFERENCES doc_lines(id),product_id INTEGER NOT NULL REFERENCES products(id),warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),qty INTEGER NOT NULL CHECK(qty>0),unit_cents INTEGER NOT NULL CHECK(unit_cents>=0));''')
 con.execute('INSERT INTO docs VALUES(1,2500)');con.execute('INSERT INTO products VALUES(1)');con.execute('INSERT INTO warehouses VALUES(1)');con.execute('INSERT INTO doc_lines VALUES(1,1)');con.execute("INSERT INTO sale_returns(sale_doc_id,refund_method,refund_cents,reason) VALUES(1,'Nakit',500,'test')");con.execute('INSERT INTO sale_return_lines(return_id,sale_line_id,product_id,warehouse_id,qty,unit_cents) VALUES(1,1,1,1,1,500)');assert con.execute('SELECT SUM(qty) FROM sale_return_lines WHERE sale_line_id=1').fetchone()[0]==1
 print('PASS: SQLite return tables/foreign keys/partial-return query')
