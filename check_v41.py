from pathlib import Path
s=Path(__file__).with_name('main.cpp').read_text()
for part in ['void choosePackForSale()', 'bool addPackToCart(', 'SELECT product_id,id FROM product_packs', 'packPrice%units!=0', 'if(packId>0)addPackToCart', '"pack_id"', 'b.packId==packId && b.packUnits==units', 'packSales=new QPushButton']:
    assert part in s, part
assert any('VERSION '+v in Path(__file__).with_name('CMakeLists.txt').read_text() for v in ['4.1','4.2'])
print('PASS V4.1 packaging POS structural checks')
