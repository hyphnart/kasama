# KasaPro V4.3 — Sprint 16: Hızlı koli açma

- 1 Koli Aç: seçili ambalajdan tek koli için kısa yol.
- İhtiyaç Kadar Koli Aç: ihtiyaç duyulan açık miktar, mevcut açık stoktan büyükse gereken en az koli adedi otomatik hesaplanır.
- Yetersiz stok, kapalı koli yetersizliği, tutarsız fiziksel kayıt, açılması yasak veya onay gerektiren ambalajlarda işlem engellenir.
- Tek bir onay adımından sonra mevcut transaction korumalı koli açma kullanılır.
- Fiziksel koli açma toplam POS stok miktarını değiştirmez. Satışı tamamlamaz.
- Önceden var olan koli yönetimi ve stok kontrolü korunmuştur.

## Sınır
POS satış / iade / depo hareketlerinin fiziksel açık adet defterine tam entegrasyonu yoktur. Otomatik POS koli açma henüz etkin değildir. Windows Qt6/MSVC derlemesi bu ortamda doğrulanmadı.

## Sanal ekip
2 Scrum, 12 mimari, 4 backend, 4 frontend, 7 mobil, 4 UI/UX, 4 QA, 4 DevOps, 2 iş analisti. Bu bir uzmanlık işbölümü modelidir; 43 gerçek çalışan iddia edilmez.
