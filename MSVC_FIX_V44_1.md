# KasaPro V4.4.1 — MSVC derleme hatası düzeltmesi

GitHub Actions / Qt 6.8.3 / MSVC için kullanıcının paylaştığı iki C++ derleme hatası düzeltildi.

1. **SupervisorGate.h C2039**: `QByteArray::value(i)` Qt6 API üyesi değildir. Hash ve türetilen anahtar boyutları 32 bayt doğrulandıktan sonra `at(i)` kullanılıyor.
2. **main.cpp C2660**: `CartonOpeningCenter` içindeki statik `exec(QSqlDatabase, ...)`, kalıtımla gelen `QDialog::exec()` fonksiyonunu gizliyordu. SQL yardımcısı `executeSql` olarak yeniden adlandırıldı ve tüm çağrıları değiştirildi. `dlg.exec()` artık `QDialog::exec()` çağırır.
3. `QSqlQuery` C4996 yalnızca derleyici uyarısıdır; bu ekran görüntüsünde derlemeyi durdurmamaktadır.

**Kontrol:** Kaynak düzeyinde iki hata doğrulandı ve düzeltildi. Qt6/MSVC derlemesi burada yapılamadığından yeni CI sonucunu kontrol etmek gerekir.
