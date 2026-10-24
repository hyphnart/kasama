# KasaPro V3.4 — Sprint 07: Barkod Etiket Merkezi

V3.3 kaynakları korunarak POS > Barkod Yönetimi ekranına **Etiket Yazdır / PDF** komutu eklendi. Seçili alternatif barkod veya ürünün ana barkodu / SKU'su kullanılır.

- Gerçek çizgiler çizilerek Code 39 sembolojisi üretilir (sadece yazı değildir).
- 40×25, 50×30, 60×40 mm; 1–50 etiket; Windows yazıcı ve PDF çıkışı.
- Desteklenen Code 39 karakterleri: A-Z, 0-9, boşluk, - . $ / + %.
- EAN/GS1 standardına uygun barkod oluşturulmaz; Code 39 uyumlu okuyucu gerektirir. Farklı semboloji seçimi ve EAN-13 çıktısı sonraki sprinttir.
- Stok, satış ve veritabanı tablolarında değişiklik yapılmadı. Varyantların özel fotoğrafları ve kesirli stok motoru henüz yapılmadı.

## Çalıştırma / QA

`python check_v34.py` ve mevcut `check_v33.py` vb. statik kontrolleri çalıştırın. Windows EXE için `.github/workflows/windows-exe.yml` GitHub Actions iş akışını kullanın.

**Not:** Bu ortamda Qt6/MSVC ve fiziksel yazıcı derleme/testi yapılmadı. Etiket boyutu, barkod okuyucu tanıma ve yazıcı sürücüsü gerçek cihazda doğrulanmalıdır.
