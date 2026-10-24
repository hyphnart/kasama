#pragma once
#include <QtWidgets>
#include <QtSql>
#include <QImageReader>

// Every variant is a normal products row, preserving existing POS, warehouse,
// purchase, reservation and returns foreign keys. This dialog never changes stock.
class VariantManagerDialog final: public QDialog {
    QSqlDatabase database;
    int parentId;
    QTableWidget *listing=nullptr;
    QLabel *caption=nullptr;
    QString parentName;
    QString parentSku;
    qint64 parentPrice=0;
    qint64 parentCost=0;
    int parentMinimum=0;
    QString imagePath;
    void reload(){
        listing->setRowCount(0);
        QSqlQuery q(database);
        q.prepare("SELECT p.sku,v.option1,v.option2,p.barcode,p.price_cents,p.id FROM product_variants v JOIN products p ON p.id=v.child_product_id WHERE v.parent_product_id=? ORDER BY v.option1,v.option2");
        q.addBindValue(parentId);
        if(!q.exec()){QMessageBox::warning(this,"Sorgu hatası",q.lastError().text());return;}
        while(q.next()){
            const int row=listing->rowCount();listing->insertRow(row);
            for(int i=0;i<5;++i){const QString value=i==4?QLocale(QLocale::Turkish,QLocale::Turkey).toString(q.value(i).toLongLong()/100.0,'f',2):q.value(i).toString();listing->setItem(row,i,new QTableWidgetItem(value));}
            listing->item(row,0)->setData(Qt::UserRole,q.value(5));
        }
        caption->setText(QString("%1 varyant • Her varyant ayrı stok ve satış kimliği taşır.").arg(listing->rowCount()));
    }
    void editVariantPhoto(){
        if(!listing->currentItem() || listing->currentRow()<0 || !listing->item(listing->currentRow(),0)){
            QMessageBox::information(this,"Varyant fotoğrafı","Önce listeden bir varyant seçin.");return;
        }
        const int childId=listing->item(listing->currentRow(),0)->data(Qt::UserRole).toInt();
        QSqlQuery read(database);read.prepare("SELECT name,image_path FROM products WHERE id=?");read.addBindValue(childId);
        if(!read.exec()||!read.next()){QMessageBox::warning(this,"Fotoğraf","Ürün kaydı okunamadı.");return;}
        const QString productName=read.value(0).toString();
        const QString original=read.value(1).toString();
        QDialog dlg(this);dlg.setWindowTitle("Varyant Fotoğraf Yönetimi — "+productName);dlg.resize(550,480);
        auto *root=new QVBoxLayout(&dlg);
        auto *preview=new QLabel;preview->setMinimumSize(300,260);preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet("background:#f3f6fa;border:1px solid #cfd9e6;border-radius:10px;");root->addWidget(preview,1);
        auto *pathText=new QLabel;pathText->setWordWrap(true);root->addWidget(pathText);
        QString selected=original;
        auto refresh=[&](){QPixmap pix(selected);if(!pix.isNull()){preview->setPixmap(pix.scaled(QSize(430,260),Qt::KeepAspectRatio,Qt::SmoothTransformation));preview->setText(QString());}
            else{preview->setPixmap(QPixmap());preview->setText("Fotoğraf seçilmedi");}pathText->setText(selected.isEmpty()?"Fotoğraf yok":QFileInfo(selected).fileName());};
        refresh();
        auto *actions=new QHBoxLayout;auto *choose=new QPushButton("Fotoğraf Seç");auto *clear=new QPushButton("Fotoğrafı Kaldır");actions->addWidget(choose);actions->addWidget(clear);actions->addStretch();root->addLayout(actions);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);root->addWidget(buttons);
        connect(choose,&QPushButton::clicked,&dlg,[&]{const QString candidate=QFileDialog::getOpenFileName(&dlg,"Varyant fotoğrafı",QString(),"Görsel (*.jpg *.jpeg *.png *.webp)");if(candidate.isEmpty())return;
            QImageReader reader(candidate);if(!reader.canRead()||reader.size().width()>12000||reader.size().height()>12000||QFileInfo(candidate).size()>15*1024*1024){QMessageBox::warning(&dlg,"Fotoğraf","Geçerli bir görsel seçin (en fazla 15 MB, 12000 px).");return;}selected=candidate;refresh();});
        connect(clear,&QPushButton::clicked,&dlg,[&]{selected.clear();refresh();});
        connect(buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
        if(dlg.exec()!=QDialog::Accepted || selected==original)return;
        QString stored=selected;
        if(!selected.isEmpty()){
            const QString directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/product_images";
            if(!QDir().mkpath(directory)){QMessageBox::warning(this,"Fotoğraf","Fotoğraf klasörü oluşturulamadı.");return;}
            // User-selected files are copied into application storage; no modification to originals.
            const QString extension=QFileInfo(selected).suffix().toLower();
            stored=directory+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+"."+extension;
            if(!QFile::copy(selected,stored)){QMessageBox::warning(this,"Fotoğraf","Fotoğraf uygulama klasörüne kopyalanamadı.");return;}
        }
        QSqlQuery update(database);update.prepare("UPDATE products SET image_path=? WHERE id=?");update.addBindValue(stored);update.addBindValue(childId);
        if(!update.exec()||update.numRowsAffected()!=1){if(!stored.isEmpty()&&stored!=selected)QFile::remove(stored);QMessageBox::warning(this,"Fotoğraf","Kayıt güncellenemedi: "+update.lastError().text());return;}
        reload();
    }
    void addVariant(){
        QDialog dialog(this);dialog.setWindowTitle("Yeni ürün varyantı");dialog.resize(570,380);
        auto *layout=new QVBoxLayout(&dialog);
        auto *form=new QFormLayout;layout->addLayout(form);
        auto *sku=new QLineEdit(parentSku+"-V"+QString::number(listing->rowCount()+1));
        auto *option1=new QLineEdit;option1->setPlaceholderText("Örn. 42, XL, 10 mm");
        auto *option2=new QLineEdit;option2->setPlaceholderText("Örn. Siyah");
        auto *barcode=new QLineEdit;barcode->setMaxLength(64);
        auto *price=new QDoubleSpinBox;price->setRange(0,100000000);price->setDecimals(2);price->setValue(parentPrice/100.0);price->setSuffix(" TL");
        form->addRow("Varyant stok kodu",sku);form->addRow("Beden / numara / ölçü",option1);form->addRow("Renk / ikinci seçenek",option2);form->addRow("Benzersiz barkod",barcode);form->addRow("Satış fiyatı",price);
        auto *warning=new QLabel("Varyant yeni bir ürün stok kimliği olarak açılır. Başlangıç stoğu sıfırdır. Stok girişini Depo / Stok bölümünden yapın.");warning->setWordWrap(true);layout->addWidget(warning);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;
        if(sku->text().trimmed().isEmpty()||option1->text().trimmed().isEmpty()){QMessageBox::warning(this,"Eksik bilgi","Stok kodu ve ilk varyant değeri gerekli.");return;}
        // A barcode already used as an alternative must not be reused.
        if(!barcode->text().trimmed().isEmpty()){
            QSqlQuery conflict(database);conflict.prepare("SELECT 1 FROM products WHERE barcode=? OR sku=? UNION SELECT 1 FROM product_barcodes WHERE barcode=? LIMIT 1");
            conflict.addBindValue(barcode->text().trimmed());conflict.addBindValue(barcode->text().trimmed());conflict.addBindValue(barcode->text().trimmed());
            if(!conflict.exec()||conflict.next()){QMessageBox::warning(this,"Barkod","Barkod zaten kullanımda veya kontrol edilemedi.");return;}
        }
        if(!database.transaction()){QMessageBox::warning(this,"Veritabanı",database.lastError().text());return;}
        QSqlQuery insert(database);
        insert.prepare("INSERT INTO products(sku,barcode,name,price_cents,cost_cents,minimum_qty,image_path) VALUES(?,?,?,?,?,?,?)");
        insert.addBindValue(sku->text().trimmed());insert.addBindValue(barcode->text().trimmed());
        QString name=parentName+" / "+option1->text().trimmed();if(!option2->text().trimmed().isEmpty())name+=" / "+option2->text().trimmed();
        insert.addBindValue(name);insert.addBindValue(qRound64(price->value()*100));insert.addBindValue(parentCost);insert.addBindValue(parentMinimum);insert.addBindValue(imagePath);
        bool ok=insert.exec();QString error=insert.lastError().text();
        if(ok){QSqlQuery relation(database);relation.prepare("INSERT INTO product_variants(parent_product_id,child_product_id,option1,option2) VALUES(?,?,?,?)");relation.addBindValue(parentId);relation.addBindValue(insert.lastInsertId());relation.addBindValue(option1->text().trimmed());relation.addBindValue(option2->text().trimmed());ok=relation.exec();error=relation.lastError().text();}
        if(ok)ok=database.commit();
        if(!ok){database.rollback();QMessageBox::warning(this,"Varyant oluşturulamadı",error);return;}
        reload();
    }
    // Matrix creator: every combination is atomic; duplicate combinations are skipped.
    void createMatrix(){
        QDialog dialog(this); dialog.setWindowTitle("Toplu varyant matrisi"); dialog.resize(610,390);
        auto *layout=new QVBoxLayout(&dialog);
        auto *form=new QFormLayout;layout->addLayout(form);
        auto *sizes=new QLineEdit("38,39,40,41,42,43");
        auto *colors=new QLineEdit("Siyah,Beyaz");
        auto *prefix=new QLineEdit(parentSku);prefix->setMaxLength(64);
        form->addRow("Beden / Numara / Ölçüler (virgülle)",sizes);
        form->addRow("Renkler / Diğer seçenekler (virgülle)",colors);
        form->addRow("Stok kodu öneki",prefix);
        auto *note=new QLabel("Oluşan her kombinasyon ayrı bir stok kimliği taşır. Barkodlar boş bırakılır; gerçek barkodları daha sonra atayın. Var olan kombinasyonlar tekrar oluşturulmaz. Başlangıç stoğu 0'dır.");
        note->setWordWrap(true);layout->addWidget(note);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;
        auto splitUnique=[](const QString &input){QStringList values;for(QString v:input.split(',',Qt::SkipEmptyParts)){v=v.trimmed();if(!v.isEmpty()&&!values.contains(v,Qt::CaseInsensitive))values.append(v);}return values;};
        const QStringList first=splitUnique(sizes->text());
        QStringList second=splitUnique(colors->text());if(second.isEmpty())second.append("");
        if(first.isEmpty()||prefix->text().trimmed().isEmpty()||first.size()*second.size()>200){QMessageBox::warning(this,"Varyant matrisi","Değerleri girin. Tek seferde en çok 200 kombinasyon oluşturulabilir.");return;}
        QSet<QString> existing;
        QSqlQuery current(database);current.prepare("SELECT option1,option2 FROM product_variants WHERE parent_product_id=?");current.addBindValue(parentId);
        if(!current.exec()){QMessageBox::warning(this,"SQL",current.lastError().text());return;}
        while(current.next())existing.insert((current.value(0).toString()+"|"+current.value(1).toString()).toCaseFolded());
        if(!database.transaction()){QMessageBox::warning(this,"SQL",database.lastError().text());return;}
        int created=0;QString error;
        bool ok=true;
        for(const QString &a:first){for(const QString &b:second){
            if(existing.contains((a+"|"+b).toCaseFolded()))continue;
            // Generate deterministic ASCII SKU parts, preserving digits and simple letters.
            auto normalize=[](QString v){v=v.toUpper().normalized(QString::NormalizationForm_D);v.remove(QRegularExpression("[\\u0300-\\u036f]"));v.replace(QRegularExpression("[^A-Z0-9]+"),"-");v.remove(QRegularExpression("^-|-$"));return v.left(22);};
            QString sku=prefix->text().trimmed()+"-"+normalize(a);if(!b.isEmpty())sku+="-"+normalize(b);
            if(sku.length()>80){ok=false;error="Üretilen stok kodu çok uzun: "+sku;break;}
            QSqlQuery insert(database);
            insert.prepare("INSERT INTO products(sku,barcode,name,price_cents,cost_cents,minimum_qty,image_path) VALUES(?,'',?,?,?,?,?)");
            QString name=parentName+" / "+a;if(!b.isEmpty())name+=" / "+b;
            insert.addBindValue(sku);insert.addBindValue(name);insert.addBindValue(parentPrice);insert.addBindValue(parentCost);insert.addBindValue(parentMinimum);insert.addBindValue(imagePath);
            if(!insert.exec()){ok=false;error=insert.lastError().text();break;}
            QSqlQuery relation(database);
            relation.prepare("INSERT INTO product_variants(parent_product_id,child_product_id,option1,option2) VALUES(?,?,?,?)");
            relation.addBindValue(parentId);relation.addBindValue(insert.lastInsertId());relation.addBindValue(a);relation.addBindValue(b);
            if(!relation.exec()){ok=false;error=relation.lastError().text();break;}
            ++created;
        }if(!ok)break;}
        if(ok)ok=database.commit();
        if(!ok){database.rollback();QMessageBox::warning(this,"Varyant matrisi",error.isEmpty()?database.lastError().text():error);return;}
        reload();QMessageBox::information(this,"Tamamlandı",QString("%1 yeni varyant oluşturuldu.").arg(created));
    }
    // Move existing parent stock to a specific child in a single database transaction.
    void allocateStock(){
        if(!listing->currentItem()||!listing->item(listing->currentRow(),0)){QMessageBox::information(this,"Stok dağıtımı","Tablodan hedef varyantı seçin.");return;}
        const int childId=listing->item(listing->currentRow(),0)->data(Qt::UserRole).toInt();
        if(childId<=0)return;
        QDialog dialog(this);dialog.setWindowTitle("Ana üründen varyanta stok dağıt");
        auto *form=new QFormLayout(&dialog);
        auto *warehouse=new QComboBox;
        QSqlQuery wh(database);wh.prepare("SELECT id,name FROM warehouses ORDER BY name");
        if(!wh.exec()){QMessageBox::warning(this,"SQL",wh.lastError().text());return;}
        while(wh.next())warehouse->addItem(wh.value(1).toString(),wh.value(0));
        auto *amount=new QSpinBox;amount->setRange(1,1000000);
        auto *available=new QLabel;available->setWordWrap(true);
        auto refreshAvailable=[&]{QSqlQuery st(database);st.prepare("SELECT COALESCE(qty-reserved,0) FROM stock WHERE product_id=? AND warehouse_id=?");st.addBindValue(parentId);st.addBindValue(warehouse->currentData());int qty=0;if(st.exec()&&st.next())qty=st.value(0).toInt();available->setText("Ana üründe kullanılabilir stok: "+QString::number(qty));};
        form->addRow("Kaynak / hedef depo",warehouse);form->addRow("Kullanılabilir stok",available);form->addRow("Aktarılacak adet",amount);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(warehouse,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int){refreshAvailable();});refreshAvailable();
        if(dialog.exec()!=QDialog::Accepted)return;
        const int wid=warehouse->currentData().toInt();const int qty=amount->value();
        if(wid<=0||qty<=0)return;
        if(!database.transaction()){QMessageBox::warning(this,"SQL",database.lastError().text());return;}
        QSqlQuery deduct(database);deduct.prepare("UPDATE stock SET qty=qty-? WHERE product_id=? AND warehouse_id=? AND qty-reserved>=?");
        deduct.addBindValue(qty);deduct.addBindValue(parentId);deduct.addBindValue(wid);deduct.addBindValue(qty);
        bool ok=deduct.exec()&&deduct.numRowsAffected()==1;QString error=deduct.lastError().text();
        if(ok){QSqlQuery make(database);make.prepare("INSERT OR IGNORE INTO stock(product_id,warehouse_id,qty,reserved) VALUES(?,?,0,0)");make.addBindValue(childId);make.addBindValue(wid);ok=make.exec();error=make.lastError().text();}
        if(ok){QSqlQuery add(database);add.prepare("UPDATE stock SET qty=qty+? WHERE product_id=? AND warehouse_id=?");add.addBindValue(qty);add.addBindValue(childId);add.addBindValue(wid);ok=add.exec()&&add.numRowsAffected()==1;error=add.lastError().text();}
        for(const auto &pair:QList<QPair<int,int>>{{parentId,-qty},{childId,qty}}){
            if(!ok)break;
            QSqlQuery movement(database);movement.prepare("INSERT INTO stock_moves(product_id,warehouse_id,delta_qty,reason) VALUES(?,?,?,?)");
            movement.addBindValue(pair.first);movement.addBindValue(wid);movement.addBindValue(pair.second);movement.addBindValue(QString("Varyant stok dağıtımı: %1 → %2").arg(parentId).arg(childId));
            ok=movement.exec();error=movement.lastError().text();
        }
        if(ok)ok=database.commit();
        if(!ok){database.rollback();QMessageBox::warning(this,"Stok aktarılamadı",error.isEmpty()?"Yetersiz kullanılabilir stok veya SQL hatası":error);return;}
        QMessageBox::information(this,"Aktarıldı",QString("%1 adet varyanta aktarıldı. Toplam fiziksel stok değişmedi.").arg(qty));
    }
public:
    explicit VariantManagerDialog(QSqlDatabase connection,int id,QWidget *parent=nullptr):QDialog(parent),database(connection),parentId(id){
        setWindowTitle("KasaPro | Varyant Yönetimi");resize(840,580);setMinimumSize(640,420);
        QSqlQuery q(database);q.prepare("SELECT sku,name,price_cents,cost_cents,minimum_qty,image_path FROM products WHERE id=?");q.addBindValue(id);
        if(!q.exec()||!q.next()){QMessageBox::warning(this,"Ürün","Ürün bulunamadı.");return;}
        parentSku=q.value(0).toString();parentName=q.value(1).toString();parentPrice=q.value(2).toLongLong();parentCost=q.value(3).toLongLong();parentMinimum=q.value(4).toInt();imagePath=q.value(5).toString();
        auto *layout=new QVBoxLayout(this);layout->setContentsMargins(22,20,22,20);layout->setSpacing(13);
        auto *title=new QLabel("Varyantlar — "+parentName);title->setWordWrap(true);title->setStyleSheet("font-size:20px;font-weight:700;color:#192c48");layout->addWidget(title);
        caption=new QLabel;layout->addWidget(caption);
        listing=new QTableWidget;listing->setColumnCount(5);listing->setHorizontalHeaderLabels({"Stok kodu","Beden / Numara / Ölçü","Renk / Seçenek","Barkod","Fiyat TL"});listing->setSelectionBehavior(QAbstractItemView::SelectRows);listing->setEditTriggers(QAbstractItemView::NoEditTriggers);listing->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(listing);
        auto *footer=new QHBoxLayout;auto *create=new QPushButton("+ Varyant Ekle");auto *matrix=new QPushButton("Toplu Matris Oluştur");auto *allocate=new QPushButton("Stok Dağıt");auto *photo=new QPushButton("Varyant Fotoğrafı");auto *close=new QPushButton("Kapat");footer->addWidget(create);footer->addWidget(matrix);footer->addWidget(allocate);footer->addWidget(photo);footer->addStretch();footer->addWidget(close);layout->addLayout(footer);
        connect(create,&QPushButton::clicked,this,[this]{addVariant();});connect(matrix,&QPushButton::clicked,this,[this]{createMatrix();});connect(allocate,&QPushButton::clicked,this,[this]{allocateStock();});connect(photo,&QPushButton::clicked,this,[this]{editVariantPhoto();});connect(close,&QPushButton::clicked,this,&QDialog::accept);
        reload();
    }
};
