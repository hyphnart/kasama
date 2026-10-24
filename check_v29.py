from pathlib import Path
s=Path(__file__).with_name('main.cpp').read_text(encoding='utf-8')
checks={
 'KPI cards': 'dashboardCards=new QWidget' in s and 'dashboardValues.size()==6' in s,
 'Live KPI SQL': "date(created_at,'localtime')" in s,
 'Dashboard recent sales': "ORDER BY d.id DESC LIMIT 50" in s,
 'No fixed columns for management table': 'setSectionResizeMode(QHeaderView::Interactive)' in s,
 'Modal intact': 'class ManagementCenter final' in s,
 'Fullscreen retained': 'Qt::Key_F11' in s,
 'Payment device retained': 'DeviceConnectionCenter.h' in s,
 'Single app entry': s.count('int main(')==1,
}
for name,ok in checks.items(): print(('PASS' if ok else 'FAIL'),name)
raise SystemExit(0 if all(checks.values()) else 1)
