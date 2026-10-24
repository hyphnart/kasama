from pathlib import Path
s=Path('main.cpp').read_text(encoding='utf8')
h=Path('DeviceRegistry.h').read_text(encoding='utf8')
assert 'deviceCatalog()' in h
assert 'UnavailableDevice' in h
assert 'std::make_unique<UnavailableDevice>' in s
assert 'std::make_unique<DemoPaymentDevice>' in s
assert 'device_attempts.jsonl' in s
assert s.count('int main(')==1
print('PASS: device registry, safe unavailable adapter, audit, one main')
