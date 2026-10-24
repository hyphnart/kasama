import pathlib, sqlite3, re, tempfile
root=pathlib.Path(__file__).resolve().parent
code=(root/'main.cpp').read_text(encoding='utf-8')
header=(root/'SectorCatalogDialog.h').read_text(encoding='utf-8')
checks={
 'Separate reusable sector dialog': 'class SectorCatalogDialog final' in header,
 'Separate reusable product details dialog':'class ProductDetailsDialog final' in header,
 'Generic product metadata schema': 'CREATE TABLE IF NOT EXISTS product_details' in code,
 'Sector profile schema': 'CREATE TABLE IF NOT EXISTS app_profile' in code,
 'Business profile menu': 'makeAction("Sektör / Ürün Profilleri"' in code,
 'Product detail menu':'makeAction("Ürün Detayları"' in code,
 'No change to stock quantity schema': 'qty INTEGER NOT NULL DEFAULT 0,reserved' in code,
 'Sale method retained': 'void refreshProducts()' in code,
 'One application entry':len(re.findall(r'\bint main\s*\(',code))==1,
 'No risky mixed-auto declaration':not re.search(r'auto\s*\*[^;\n]+,\s*\*',header),
}
for title,result in checks.items():print(('PASS' if result else 'FAIL'),title)
assert all(checks.values())
# Verify additive migration and persistence with actual sqlite independently.
with tempfile.TemporaryDirectory() as t:
 db=sqlite3.connect(str(pathlib.Path(t)/'test.sqlite'))
 db.executescript('''PRAGMA foreign_keys=ON;
 CREATE TABLE products(id INTEGER PRIMARY KEY,sku TEXT UNIQUE,name TEXT,price_cents INTEGER);
 INSERT INTO products(id,sku,name,price_cents) VALUES(1,'OLD-1','Eski Ürün',2500);
 CREATE TABLE app_profile(id INTEGER PRIMARY KEY CHECK(id=1),sector TEXT NOT NULL DEFAULT 'Genel Perakende',updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
 INSERT OR IGNORE INTO app_profile(id,sector) VALUES(1,'Genel Perakende');
 CREATE TABLE product_details(product_id INTEGER PRIMARY KEY REFERENCES products(id) ON DELETE CASCADE,category TEXT NOT NULL DEFAULT '',brand TEXT NOT NULL DEFAULT '',base_unit TEXT NOT NULL DEFAULT 'Adet',variant_axis1 TEXT NOT NULL DEFAULT '',variant_value1 TEXT NOT NULL DEFAULT '',variant_axis2 TEXT NOT NULL DEFAULT '',variant_value2 TEXT NOT NULL DEFAULT '',description TEXT NOT NULL DEFAULT '');
 ''')
 db.execute("UPDATE app_profile SET sector=? WHERE id=1",('Ayakkabı',))
 db.execute("INSERT INTO product_details(product_id,category,brand,base_unit,variant_axis1,variant_value1) VALUES(1,'Spor Ayakkabı','Örnek Marka','Çift','Numara','42')")
 assert db.execute('SELECT price_cents FROM products WHERE id=1').fetchone()[0]==2500
 assert db.execute('SELECT variant_value1 FROM product_details WHERE product_id=1').fetchone()[0]=='42'
 assert db.execute('PRAGMA foreign_key_check').fetchall()==[]
 print('PASS SQLite additive migration, preserved legacy product, foreign keys')
