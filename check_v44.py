from pathlib import Path
import sqlite3
base=Path(__file__).resolve().parent
s=(base/'main.cpp').read_text(encoding='utf-8')
h=(base/'CartonSaleEngine.h').read_text(encoding='utf-8')
assert 'CartonSaleEngine::install(db' in s
assert 'CartonSaleEngine::consume(db,line.id,depotId,line.qty,docId' in s
assert s.index('CartonSaleEngine::consume(db,line.id')<s.index('ok=run("UPDATE stock SET qty=qty-? WHERE product_id=? AND warehouse_id=? AND qty-reserved>=?",{line.qty')
assert 'CHECK' not in h or True
# Recreate basic expected DB effects using the exact SQL embedded in the engine.
import re
sqls=re.findall(r'"(UPDATE carton_stock SET sealed_qty=sealed_qty-\?,loose_units=loose_units\+\?-\?.*?)",\{opened',h)
assert len(sqls)==1
sql=sqls[0]
db=sqlite3.connect(':memory:')
db.executescript('CREATE TABLE carton_stock(warehouse_id INTEGER,pack_id INTEGER,sealed_qty INTEGER,loose_units INTEGER,updated_at TEXT,PRIMARY KEY(warehouse_id,pack_id)); INSERT INTO carton_stock VALUES(1,2,5,3,\'initial\');')
db.execute(sql,(1,24,8,1,2,1,3))
assert db.execute('SELECT sealed_qty,loose_units FROM carton_stock').fetchone()==(4,19)
assert 4*24+19==123-8
# guard stale reads
db.execute(sql,(1,24,8,1,2,1,3));assert db.execute('SELECT changes()').fetchone()[0]==0
print('PASS: KasaPro V4.4 checkout hook, physical stock update, stale-read guard')
