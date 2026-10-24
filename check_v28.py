from pathlib import Path
s=(Path(__file__).parent/'main.cpp').read_text(encoding='utf-8')
for snippet in ('QGridLayout *actionGrid=nullptr','void resizeEvent(QResizeEvent *event) override','actionGrid->addWidget(button,position/columns,position%columns)','qBound(1, availableWidth/205, 4)','const QStringList allowed=visible.value'):
    assert snippet in s, snippet
assert s.count('int main(')==1
print('PASS: V2.8 toolbar checks; real compile not tested')
