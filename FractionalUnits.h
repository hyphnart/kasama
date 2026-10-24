#pragma once
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <limits>
// Fixed-point conversions. Input is 1/1000 of selected display unit.
// Output is 1/1000 of the canonical stock unit. No floating-point SQL balances.
namespace FractionalUnits {
inline QStringList choices(const QString &canonical){
 if(canonical=="Kg")return {"Kg","Gram"};
 if(canonical=="Metre")return {"Metre","Santimetre","Milimetre"};
 if(canonical=="Litre")return {"Litre","Mililitre"};
 if(canonical=="m²")return {"m²","cm²"};
 if(canonical=="m³")return {"m³","Litre (hacim)"};
 return {canonical};
}
// UI quantity passed as qint64 thousandths; divisibility makes conversion exact.
inline bool convert(qint64 selectedMilli,const QString &canonical,const QString &selected,qint64 *baseMilli,QString *error=nullptr){
 auto reject=[&](const QString &message){if(error)*error=message;return false;};
 if(!baseMilli||selectedMilli<=0)return reject("Pozitif bir miktar girin.");
 qint64 numerator=1, denominator=1;
 if(selected==canonical){}
 else if(canonical=="Kg"&&selected=="Gram")denominator=1000;
 else if(canonical=="Metre"&&selected=="Santimetre")denominator=100;
 else if(canonical=="Metre"&&selected=="Milimetre")denominator=1000;
 else if(canonical=="Litre"&&selected=="Mililitre")denominator=1000;
 else if(canonical=="m²"&&selected=="cm²")denominator=10000;
 else if(canonical=="m³"&&selected=="Litre (hacim)")denominator=1000;
 else return reject("Uyumsuz birim seçimi.");
 if(selectedMilli%denominator!=0)return reject("Bu ölçü, stok hassasiyeti olan 0,001 ana birime tam çevrilemiyor.");
 if(selectedMilli>std::numeric_limits<qint64>::max()/numerator)return reject("Miktar çok büyük.");
 *baseMilli=(selectedMilli/denominator)*numerator;
 if(*baseMilli<=0)return reject("Dönüştürülen miktar sıfır olamaz.");
 return true;
}
}
