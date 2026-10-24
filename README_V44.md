# KasaPro V4.4 — Sprint 17

## Kolay POS otomatik koli açma
- **Ana POS ödeme akışında**, fiziksel koli stoğu tutulan ürünler için ödeme kaydıyla aynı SQLite transaction içinde açık adet tüketimi ve gerektiğinde koli açma.
- 5 kapalı koli + 3 açık adet → 8 adet satış sonrası 4 kapalı koli + 19 açık adet. Ana stok 123'ten 115'e düşer.
- `carton_sale_events` satış belgesi ID, açılan koli ve tüketilen adet sayısını kaydeder.
- Bir ürünün fiziksel ambalaj defteri ana stokla eşit değilse, birden çok aktif ambalaj türü varsa veya açma yasak/onay gerektiriyorsa **satış engellenir** ve transaction geri alınır.
- Eski ana stok kaydı olan, **fiziksel koli defteri bulunmayan** ürünlerde normal POS satış akışı korunur.
- Eski **BackofficeWindow** satış akışı fiziksel koli takibi yapılmış ürünlerin satışını reddeder; bu ürünler **ana POS ekranından** satılmalıdır.

## Bilinçli sınırlar
- Yönetici onaylı koli açma hâlâ güvenli kimlik doğrulama olmadığı için engelli.
- Çoklu ambalajlı karma fiziksel stok (12'li ve 24'lü birlikte) otomatik yönetilmez. Önce mutabakat gerekir.
- Fiziksel koli defteriyle POS dışındaki transfer, sayım, iade ve bütün stok düzeltme yolları henüz birleştirilmedi. Bu yollar için işletme doğrulaması şart.
- Gerçek Qt6/MSVC Windows derlemesi, UI ve yazarkasa testleri bu ortamda yapılamadı. Kaynak/SQLite kontrolleri canlı çalışmayı garanti etmez.
