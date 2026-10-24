from pathlib import Path
h=(Path(__file__).parent/'VariantManagerDialog.h').read_text(encoding='utf-8')
main=(Path(__file__).parent/'main.cpp').read_text(encoding='utf-8')
assert 'editVariantPhoto()' in h
assert 'Varyant Fotoğrafı' in h
assert 'SELECT name,image_path FROM products WHERE id=?' in h
assert 'UPDATE products SET image_path=? WHERE id=?' in h
assert 'QFile::copy(selected,stored)' in h
assert 'QFile::remove(stored)' in h
assert 'QImageReader reader(candidate)' in h
assert 'connect(photo,&QPushButton::clicked' in h
assert 'VariantManagerDialog dlg(db,id,this)' in main
assert 'QStandardPaths::AppDataLocation' in h
print('PASS V3.5 variant image isolation, storage, validation, SQL and UI wiring static checks')
