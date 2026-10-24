from pathlib import Path
p=Path(__file__).parent
s=(p/'CartonOpeningCenter.h').read_text()
for token in ['prepareForLooseDemand','1 Koli Aç','İhtiyaç Kadar Koli Aç','demand>total-reserved','demand<=loose','cases>sealed','openCases();','sealed*per+loose>total']:
    assert token in s,token
# Pure scenario checks of the formula used in UI; not an end-to-end Qt test.
def cases(needed,loose,per):
    return 0 if needed<=loose else 1+(needed-loose-1)//per
for test,expected in [((8,3,24),1),((27,3,24),1),((28,3,24),2),((3,3,24),0),((52,3,24),3)]:
    assert cases(*test)==expected,(test,expected)
print('PASS: V4.3 hızlı koli açma statik ve hesaplama kontrolleri')
