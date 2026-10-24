from pathlib import Path
s=Path('main.cpp').read_text(encoding='utf8')
h=Path('SupervisorGate.h').read_text(encoding='utf8')
assert s.count('int main(')==1
assert 'SupervisorGate::initialize(db,' in s
assert 'SupervisorGate::authorize(&dialog' in s
assert 'physical!=expected && !SupervisorGate::authorize' in s
assert 'deriveKeyPbkdf2' in h
assert 'approval_audit' in h
print('PASS: V2.4 static integration checks')
