#pragma once
#include <QtWidgets>
#include <QPrinter>
#include <QPrintDialog>
#include <QPainter>
#include <QPageSize>
#include <QFileDialog>

// Code 39 barcodes: uppercase ASCII [0-9 A-Z - . space $ / + %].
// Each symbol contains nine alternating bar/space elements (3 wide, 6 narrow).
namespace BarcodeLabelPrinter {
inline const QHash<QChar,QString>& patterns(){
 static const QHash<QChar,QString> m={
 {'0',"nnnwwnwnn"},{'1',"wnnwnnnnw"},{'2',"nnwwnnnnw"},{'3',"wnwwnnnnn"},
 {'4',"nnnwwnnnw"},{'5',"wnnwwnnnn"},{'6',"nnwwwnnnn"},{'7',"nnnwnnwnw"},
 {'8',"wnnwnnwnn"},{'9',"nnwwnnwnn"},{'A',"wnnnnwnnw"},{'B',"nnwnnwnnw"},
 {'C',"wnwnnwnnn"},{'D',"nnnnwwnnw"},{'E',"wnnnwwnnn"},{'F',"nnwnwwnnn"},
 {'G',"nnnnnwwnw"},{'H',"wnnnnwwnn"},{'I',"nnwnnwwnn"},{'J',"nnnnwwwnn"},
 {'K',"wnnnnnnww"},{'L',"nnwnnnnww"},{'M',"wnwnnnnwn"},{'N',"nnnnwnnww"},
 {'O',"wnnnwnnwn"},{'P',"nnwnwnnwn"},{'Q',"nnnnnnwww"},{'R',"wnnnnnwwn"},
 {'S',"nnwnnnwwn"},{'T',"nnnnwnwwn"},{'U',"wwnnnnnnw"},{'V',"nwwnnnnnw"},
 {'W',"wwwnnnnnn"},{'X',"nwnnwnnnw"},{'Y',"wwnnwnnnn"},{'Z',"nwwnwnnnn"},
 {'-',"nwnnnnwnw"},{'.',"wwnnnnwnn"},{' ',"nwwnnnwnn"},{'$',"nwnwnwnnn"},
 {'/',"nwnwnnnwn"},{'+',"nwnnnwnwn"},{'%',"nnnwnwnwn"},{'*',"nwnnwnwnn"}
 };return m;
}
inline bool supported(const QString &input){
 const QString data=input.trimmed().toUpper();
 if(data.isEmpty()||data.size()>48)return false;
 for(QChar c:data)if(c=='*'||!patterns().contains(c))return false;
 return true;
}
inline void drawLabel(QPainter &p,const QString &name,const QString &code,int widthMm,int heightMm){
 p.save();p.setRenderHint(QPainter::Antialiasing,false);
 p.setWindow(0,0,widthMm*10,heightMm*10);
 const int w=widthMm*10,h=heightMm*10;
 p.fillRect(0,0,w,h,Qt::white);p.setPen(Qt::black);
 QFont title=p.font();title.setPixelSize(qMax(12,h/12));title.setBold(true);p.setFont(title);
 p.drawText(QRect(12,10,w-24,h/5),Qt::AlignCenter|Qt::TextSingleLine,p.fontMetrics().elidedText(name,Qt::ElideRight,w-30));
 const QString full="*"+code.trimmed().toUpper()+"*";
 int units=0;for(QChar c:full){const QString pat=patterns().value(c);for(QChar x:pat)units+=(x=='w'?3:1);units+=1;}
 const int left=18,right=w-18,barTop=h/4,barHeight=h/2;
 const qreal module=qreal(right-left)/units; qreal x=left;
 for(QChar c:full){const QString pat=patterns().value(c);for(int i=0;i<pat.size();++i){qreal segment=module*(pat.at(i)=='w'?3:1);if(i%2==0)p.fillRect(QRectF(x,barTop,segment,barHeight),Qt::black);x+=segment;}x+=module;}
 QFont footer=p.font();footer.setPixelSize(qMax(13,h/11));footer.setBold(false);p.setFont(footer);
 p.drawText(QRect(10,h*3/4,w-20,h/5),Qt::AlignCenter,code.toUpper());p.restore();
}
inline void show(QWidget *parent,const QString &productName,const QString &initialCode){
 QDialog dlg(parent);dlg.setWindowTitle("Barkod Etiketi | Code 39");dlg.resize(470,280);
 auto *layout=new QVBoxLayout(&dlg);
 auto *note=new QLabel("Code 39 etiketleri: stok kodu veya desteklenen barkod karakterleri. EAN/GS1 etiket standardı değildir.");note->setWordWrap(true);layout->addWidget(note);
 auto *form=new QFormLayout;auto *code=new QLineEdit(initialCode);code->setMaxLength(48);form->addRow("Etiket kodu",code);
 auto *paper=new QComboBox;paper->addItem("50 × 30 mm",QSize(50,30));paper->addItem("40 × 25 mm",QSize(40,25));paper->addItem("60 × 40 mm",QSize(60,40));form->addRow("Kağıt boyutu",paper);
 auto *count=new QSpinBox;count->setRange(1,50);form->addRow("Etiket adedi",count);layout->addLayout(form);
 auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel);auto *print=buttons->addButton("Yazdır",QDialogButtonBox::AcceptRole);auto *pdf=buttons->addButton("PDF'e aktar",QDialogButtonBox::ActionRole);layout->addWidget(buttons);
 QObject::connect(buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
 auto job=[&](bool exportPdf){
  const QString value=code->text().trimmed().toUpper();if(!supported(value)){QMessageBox::warning(&dlg,"Code 39","Kod 1–48 karakter olmalı; A-Z, 0-9, boşluk ve - . $ / + % karakterleri kullanılabilir.");return;}
  const QSize sz=paper->currentData().toSize();QPrinter printer(QPrinter::HighResolution);
  printer.setPageSize(QPageSize(QSizeF(sz.width(),sz.height()),QPageSize::Millimeter,"KasaPro Etiket"));
  printer.setPageMargins(QMarginsF(0,0,0,0),QPageLayout::Millimeter);
  if(exportPdf){QString path=QFileDialog::getSaveFileName(&dlg,"Etiket PDF",QDir::homePath()+"/kasapro-etiket.pdf","PDF (*.pdf)");if(path.isEmpty())return;if(!path.endsWith(".pdf",Qt::CaseInsensitive))path+=".pdf";printer.setOutputFormat(QPrinter::PdfFormat);printer.setOutputFileName(path);}
  else {QPrintDialog pd(&printer,&dlg);if(pd.exec()!=QDialog::Accepted)return;}
  QPainter painter;if(!painter.begin(&printer)){QMessageBox::critical(&dlg,"Yazdırma","Yazıcı / PDF çıktısı başlatılamadı.");return;}
  for(int i=0;i<count->value();++i){if(i>0)printer.newPage();drawLabel(painter,productName,value,sz.width(),sz.height());}
  painter.end();dlg.accept();
 };
 QObject::connect(print,&QPushButton::clicked,&dlg,[&]{job(false);});QObject::connect(pdf,&QPushButton::clicked,&dlg,[&]{job(true);});dlg.exec();
}
}
