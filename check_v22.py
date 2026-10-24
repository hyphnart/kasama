from pathlib import Path
import re
root=Path(__file__).parent
main=(root/'main.cpp').read_text(encoding='utf-8')
hdr=(root/'DeviceConnectionCenter.h').read_text(encoding='utf-8')
cmake=(root/'CMakeLists.txt').read_text(encoding='utf-8')
checks={
 'One executable entrypoint': main.count('int main(')==1,
 'Connection center included': '#include "DeviceConnectionCenter.h"' in main,
 'Connection center wired': 'DeviceConnectionCenter center(db,&dlg)' in main,
 'Network linked': 'Qt6::Network' in cmake,
 'Connection migration': 'CREATE TABLE IF NOT EXISTS payment_device_connections' in hdr,
 'Connection checks audit': 'CREATE TABLE IF NOT EXISTS payment_connection_checks' in hdr,
 'TCP reachability label': 'TCP erişilebilir' in hdr,
 'No live device payment claimed': 'SDK\'sı olmayan cihazlarda canlı tahsilat kapalıdır' in hdr,
 'Reconciliation states': "('Bekliyor','Bilinmiyor','Baslatilmadi','Hata')" in hdr,
 'Existing payment store': 'PaymentAttemptStore::migrate' in main,
 'No new mixed auto declarations': not re.search(r'auto\s*\*[^;\n]*,\s*\*[^;\n]*;',hdr),
}
for k,v in checks.items():print(('PASS' if v else 'FAIL')+': '+k)
if not all(checks.values()):raise SystemExit(1)
