from pathlib import Path
import re
root=Path(__file__).parent
main=(root/'main.cpp').read_text(encoding='utf-8')
h=(root/'BarcodeLabelPrinter.h').read_text(encoding='utf-8')
assert '#include "BarcodeLabelPrinter.h"' in main
assert 'Etiket Yazdır / PDF' in main
assert 'BarcodeLabelPrinter::show(&dialog,name,labelCode)' in main
assert 'QPrintDialog' in h and 'QPrinter::PdfFormat' in h
assert 'QPageSize' in h and 'painter.begin(&printer)' in h
assert 'setPageMargins(QMarginsF(0,0,0,0)' in h
pairs=re.findall(r"\{'([^']+)',\"([nw]+)\"\}",h)
assert len(pairs)==44, len(pairs)
assert len(set(a for a,b in pairs))==len(pairs)
for key,pattern in pairs:
    assert len(pattern)==9,(key,pattern)
    assert pattern.count('w')==3,(key,pattern)
assert 'if(i>0)printer.newPage()' in h
assert 'project(KasaPro VERSION ' in (root/'CMakeLists.txt').read_text()
print('PASS: V3.4 barcode label static checks, 44 code39 symbol patterns.')
