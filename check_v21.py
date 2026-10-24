from pathlib import Path
s=Path('main.cpp').read_text(encoding='utf-8')
p=Path('PaymentAttemptStore.h').read_text(encoding='utf-8')
d=Path('PaymentDevice.h').read_text(encoding='utf-8')
checks={
  'single_main':s.count('int main(')==1,
  'migration': 'PaymentAttemptStore::migrate(db' in s,
  'create_before_driver': s.index('PaymentAttemptStore::create(db')<s.index('driver->startPayment(request)'),
  'persist_device_result':'PaymentAttemptStore::update(db' in s,
  'history_view':'Cihaz İşlem Geçmişi' in s,
  'dedup_reference':'merchant_reference TEXT NOT NULL UNIQUE' in p,
  'no_card_pan_in_store':'pan' not in p.lower() and 'cvv' not in p.lower(),
  'demo_no_approval':'return {PaymentStatus::Pending' in d,
  'device_stubs_disabled':'driverAvailable = false' in Path('DeviceRegistry.h').read_text(encoding='utf-8'),
}
for k,v in checks.items(): print(('PASS' if v else 'FAIL'),k)
assert all(checks.values())
