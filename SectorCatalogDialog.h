#pragma once
#include <QtWidgets>
#include <QtSql>

// Catalog metadata is intentionally separate from stock quantity semantics.
// Variants are descriptive until stock-by-variant and order-line foreign keys are migrated.
namespace KasaCatalog {
inline QStringList sectors(){return {"Genel Perakende","Market / Gıda","Nalbur / Hırdavat","Butik / Giyim","Ayakkabı","Elektronik","Kozmetik","Kırtasiye","Yedek Parça","Ev / Mobilya"};}
inline QStringList units(){return {"Adet","Paket","Koli","Çift","Metre","m²","m³","Kg","Gram","Litre","Mililitre","Rulo","Takım","Kutu"};}
inline QStringList axes(const QString &sector){
 if(sector=="Butik / Giyim")return {"Beden","Renk","Model","Kalıp","Sezon"};
 if(sector=="Ayakkabı")return {"Numara","Renk","Model","Kalıp"};
 if(sector=="Nalbur / Hırdavat")return {"Ölçü","Çap","Uzunluk","Renk","Marka"};
 if(sector=="Elektronik")return {"Kapasite","Renk","Model","Voltaj"};
 if(sector=="Yedek Parça")return {"Uyumlu Model","Seri","Ölçü","Marka"};
 return {"Renk","Beden","Numara","Ölçü","Model","Seri"};
}
inline QString currentSector(){QSqlQuery q("SELECT sector FROM app_profile WHERE id=1");return q.next()?q.value(0).toString():"Genel Perakende";}
inline QLabel* caption(const QString& s){QLabel *l=new QLabel(s);l->setWordWrap(true);l->setStyleSheet("color:#66768a;font-size:12px");return l;}
}

class SectorCatalogDialog final:public QDialog{
public:
 explicit SectorCatalogDialog(QWidget *parent=nullptr):QDialog(parent){
  setWindowTitle("KasaPro | İşletme / Sektör Profili");setMinimumSize(540,340);resize(690,460);
  auto *v=new QVBoxLayout(this);v->setContentsMargins(24,22,24,22);v->setSpacing(16);
  auto *title=new QLabel("İşletme Profili");title->setStyleSheet("font-size:23px;font-weight:750;color:#18324c");v->addWidget(title);
  v->addWidget(KasaCatalog::caption("Profil yalnızca önerilen alanları ve sektörel çalışma düzenini belirler. Mevcut ürünleri, stokları ve satışları değiştirmez."));
  auto *form=new QFormLayout;auto *sector=new QComboBox;sector->addItems(KasaCatalog::sectors());int idx=sector->findText(KasaCatalog::currentSector());sector->setCurrentIndex(qMax(0,idx));sector->setMinimumHeight(42);form->addRow("İşletme türü",sector);v->addLayout(form);
  auto *guide=new QLabel;guide->setWordWrap(true);guide->setStyleSheet("background:#eff5fa;border-radius:10px;padding:14px;color:#304b68");v->addWidget(guide);
  auto refreshGuide=[=]{QString s=sector->currentText();guide->setText("Önerilen ürün nitelikleri: "+KasaCatalog::axes(s).join(" · ")+"\\nÖlçü birimleri: "+KasaCatalog::units().join(" · "));};
  connect(sector,&QComboBox::currentTextChanged,this,[=](const QString&){refreshGuide();});refreshGuide();
  v->addStretch();auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);v->addWidget(buttons);
  connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,this,[=]{QSqlQuery q;q.prepare("UPDATE app_profile SET sector=?,updated_at=CURRENT_TIMESTAMP WHERE id=1");q.addBindValue(sector->currentText());if(!q.exec()){QMessageBox::warning(this,"Kayıt Hatası",q.lastError().text());return;}accept();});
 }
};

class ProductDetailsDialog final:public QDialog{
public:
 explicit ProductDetailsDialog(int productId,QWidget *parent=nullptr):QDialog(parent){
  setWindowTitle("KasaPro | Ürün Sektörel Detayları");setMinimumSize(600,550);resize(740,650);
  auto *v=new QVBoxLayout(this);v->setContentsMargins(24,20,24,20);
  QSqlQuery product;product.prepare("SELECT sku,name FROM products WHERE id=?");product.addBindValue(productId);
  if(!product.exec()||!product.next()){QMessageBox::warning(this,"Ürün","Ürün bulunamadı.");return;}
  auto *title=new QLabel(product.value(1).toString());title->setStyleSheet("font-size:21px;font-weight:bold;color:#18324c");v->addWidget(title);
  v->addWidget(KasaCatalog::caption("Stok kodu: "+product.value(0).toString()+"  •  Ürün sınıflandırması ve açıklayıcı nitelikler"));
  auto *f=new QFormLayout;f->setSpacing(12);v->addLayout(f);
  auto *category=new QLineEdit;auto *brand=new QLineEdit;auto *unit=new QComboBox;unit->setEditable(true);unit->addItems(KasaCatalog::units());
  auto *axis1=new QComboBox;axis1->setEditable(true);axis1->addItems(KasaCatalog::axes(KasaCatalog::currentSector()));
  auto *value1=new QLineEdit;auto *axis2=new QComboBox;axis2->setEditable(true);axis2->addItems(KasaCatalog::axes(KasaCatalog::currentSector()));auto *value2=new QLineEdit;
  auto *description=new QTextEdit;description->setMaximumHeight(115);
  f->addRow("Kategori",category);f->addRow("Marka",brand);f->addRow("Temel stok birimi",unit);
  f->addRow("1. nitelik türü",axis1);f->addRow("1. nitelik değeri",value1);
  f->addRow("2. nitelik türü",axis2);f->addRow("2. nitelik değeri",value2);f->addRow("Açıklama",description);
  QSqlQuery q;q.prepare("SELECT category,brand,base_unit,variant_axis1,variant_value1,variant_axis2,variant_value2,description FROM product_details WHERE product_id=?");q.addBindValue(productId);
  if(q.exec()&&q.next()){
   category->setText(q.value(0).toString());brand->setText(q.value(1).toString());unit->setCurrentText(q.value(2).toString());
   axis1->setCurrentText(q.value(3).toString());value1->setText(q.value(4).toString());axis2->setCurrentText(q.value(5).toString());value2->setText(q.value(6).toString());description->setPlainText(q.value(7).toString());
  }
  v->addWidget(KasaCatalog::caption("Not: Beden/renk/numara burada tanımlayıcı bilgidir. Her varyant için ayrı stok ve barkod henüz etkin değildir. Kg/metre gibi kesirli satış miktarları mevcut POS motorunda desteklenmez."));
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);v->addWidget(buttons);
  connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,this,[=]{
   if(unit->currentText().trimmed().isEmpty()){QMessageBox::warning(this,"Birim","Temel birim boş olamaz.");return;}
   QSqlQuery save;save.prepare("INSERT INTO product_details(product_id,category,brand,base_unit,variant_axis1,variant_value1,variant_axis2,variant_value2,description) VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(product_id) DO UPDATE SET category=excluded.category,brand=excluded.brand,base_unit=excluded.base_unit,variant_axis1=excluded.variant_axis1,variant_value1=excluded.variant_value1,variant_axis2=excluded.variant_axis2,variant_value2=excluded.variant_value2,description=excluded.description");
   for(const QVariant &x:QVariantList{productId,category->text().trimmed(),brand->text().trimmed(),unit->currentText().trimmed(),axis1->currentText().trimmed(),value1->text().trimmed(),axis2->currentText().trimmed(),value2->text().trimmed(),description->toPlainText().trimmed()})save.addBindValue(x);
   if(!save.exec()){QMessageBox::warning(this,"Kayıt Hatası",save.lastError().text());return;}accept();
  });
 }
};
