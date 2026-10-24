# KasaPro V4.2 — Sprint 15 | Kapalı koli / açık adet

V4.1 üzerine hazırlanmıştır. `CartonOpeningCenter.h` yeni fiziksel ambalaj defteri ve manuel koli açma penceresidir. Ambalaj Merkezi üzerinden yeni koli mal kabulü artık hem toplam adet stokunu hem kapalı koli sayısını aynı transaction içinde günceller. Manuel açma kapalı koli sayısını azaltır, açılmış adet sayısını artırır; toplam stok `stock.qty` değişmez.

**Sınırlar:** Önceki sürümlerde kaydedilmiş mal kabuller otomatik olarak kapalı koliye çevrilmez. Kasa/POS satışı, iade, transfer veya manuel stok düzeltme fiziksel ambalaj defterini güncellemez. Açılmış adet bakiyesi bu yüzden operasyonel olarak nihai değildir. 'Yönetici onaylı' seçeneği güvenli yetkilendirme tamamlanana kadar açmayı engeller; 'Açılamaz' kuralı da açmayı engeller. Otomatik koli açma henüz uygulanmamıştır. Canlı kullanım için bütün stok yollarının birleştirilmesi ve MSVC uçtan uca test gereklidir.

## Test
`python check_v42.py` SQLite ve kaynak yapısı kontrolleri. GitHub Actions Windows derlemesi ayrıca doğrulanmalıdır.
