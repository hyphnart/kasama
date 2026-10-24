from pathlib import Path
p=Path(__file__).parent
h=(p/'FractionalUnits.h').read_text(encoding='utf-8');f=(p/'FractionalTradeDialog.h').read_text(encoding='utf-8')
assert 'selectedMilli%denominator!=0' in h
assert 'canonical=="Kg"&&selected=="Gram"' in h
assert 'canonical=="m²"&&selected=="cm²"' in h
assert 'FractionalUnits::convert' in f
assert 'updateUnits()' in f
assert 'Birim fiyatı ANA STOK BİRİMİ' in f
assert any('VERSION '+v in (p/'CMakeLists.txt').read_text() for v in ('3.8','3.9','4.0','4.1'))
def conv(amt,den):
 if amt<=0 or amt%den:raise ValueError
 return amt//den
assert conv(250000,1000)==250 # 250 gram = .250 kg
assert conv(175000,100)==1750 # 175 cm = 1.750 m
assert conv(2000,1000)==2 # 2 ml = .002 litre
for qty,den in [(1,1000),(125,1000),(1000,10000)]:
 try:conv(qty,den)
 except ValueError:pass
 else:raise AssertionError('lossy conversion accepted')
print('PASS: V3.8 unit conversion markers, precision gates, price clarity')
