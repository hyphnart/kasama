from pathlib import Path
s=(Path(__file__).parent/'main.cpp').read_text(encoding='utf-8')
for term in ['void chooseVariantForSale(int parentId)', 'int availableVariantCount(int parentId)', 'const bool parentWithVariants=hasVariants(id);', 'if(parentWithVariants)chooseVariantForSale(id);else addToCart(id);', 'if(term.isEmpty())', 's.qty>s.reserved', 'addToCart(id))dialog.accept()', 'product_variants']:
 assert term in s,term
assert s.count('class PosWindow final')==1
print('PASS: V3.3 static POS variant integration checks')
