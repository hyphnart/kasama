from pathlib import Path
s=Path(__file__).with_name('main.cpp').read_text(encoding='utf-8')
h=Path(__file__).with_name('UiDesign.h').read_text(encoding='utf-8')
checks={
 'single entrypoint':s.count('int main(')==1,
 'shared design header': '#include "UiDesign.h"' in s,
 'management navigation': 'UiDesign::kSidebarWidth' in s,
 'tables polished': 'UiDesign::polishTable(listing)' in s,
 'toolbar minimum targets': 'UiDesign::polishAction(button)' in s,
 'two row POS header':'topColumn->addLayout(identityRow)' in s and 'topColumn->addLayout(controlsRow)' in s,
 'no forced 990px header':'top->setMinimumWidth(990)' not in s,
 'management multirow actions':'QGridLayout(toolbarPanel)' in s,
 'F11 shortcuts': s.count('Qt::Key_F11')>=2,
 'preserved device and payment code':'PaymentDevice' in s and 'pos_payments' in s,
 'preserved purchase workflow':'purchase_orders' in s,
 'shared styling hook': 'UiDesign::globalQss()' in s,
 'main QApplication stylesheet syntax': ')QSS"));app.setApplicationName' in s,
 'style metrics': 'kTouchTarget = 44' in h,
}
for name,ok in checks.items():print(('PASS' if ok else 'FAIL')+': '+name)
raise SystemExit(0 if all(checks.values()) else 1)
