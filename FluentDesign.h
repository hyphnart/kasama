#pragma once
#include <QtWidgets>

// ---------------------------------------------------------------------------
// FluentDesign — KasaPro için Microsoft Fluent 2 / Windows 11 görünümü.
// Tek kaynak: renkler, yazı ölçeği, köşe yarıçapları ve bileşen stilleri
// burada. Veritabanı veya iş mantığı içermez.
//
// Kullanım:   FluentDesign::apply(app);            // main() içinde, bir kez
//             FluentDesign::setRole(btn,"accent"); // vurgulu (mavi) buton
//             FluentDesign::setRole(lbl,"title");  // yazı rolü
// ---------------------------------------------------------------------------
namespace FluentDesign {

// 4 px ızgara: boşluklar 4/8/12/16/24; köşeler yalnızca iki değer.
inline constexpr int kRadiusControl = 4;   // buton, giriş alanı, liste öğesi
inline constexpr int kRadiusSurface = 8;   // kart, diyalog, tablo
inline constexpr int kControlHeight = 36;  // masaüstü
inline constexpr int kTouchHeight   = 44;  // POS / dokunmatik

// NOT: QSS'te min-height kullanılmaz; aksi halde koddaki setMinimumHeight()
// (dokunmatik hedefler) ezilir.
// Yeniden polish gerektirir: role dinamik özellik olarak okunur.
inline void setRole(QWidget *w, const char *role) {
    if (!w) return;
    w->setProperty("role", QString::fromLatin1(role));
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

inline QString qss() {
    QString s = QString::fromUtf8(R"QSS(
/* ---------- Temel ---------- */
QMainWindow, QDialog { background:@bg; color:@text; }
QLabel { background:transparent; color:@text; }
QLabel[role="caption"], QLabel#muted { color:@text2; font-size:12px; }
QLabel[role="subtitle"], QLabel#sectionTitle { font-size:16px; font-weight:600; color:@text; letter-spacing:0; }
QLabel[role="title"], QLabel#title, QLabel#heading { font-size:24px; font-weight:600; color:@text; padding:0; }
QLabel[role="display"] { font-size:28px; font-weight:600; color:@text; }
QLabel[role="total"] { font-size:20px; font-weight:600; color:@text; }
QLabel[role="success"] { color:@success; font-weight:600; }
QLabel[role="danger"]  { color:@danger;  font-weight:600; }
QLabel#stats, QLabel#warehouseSummary {
    background:@brandTint; color:@brandPress; border:1px solid @brandTint2;
    border-radius:@rS; padding:12px 16px; font-size:14px; font-weight:600; }
QLabel#productPreview { background:@surface; border:1px solid @stroke; border-radius:@rS; color:@text3; }

/* ---------- Yüzeyler ---------- */
QFrame#card, QFrame#kpiCard, QFrame#body, QFrame#posHeader {
    background:@surface; border:1px solid @stroke; border-radius:@rS; }
QWidget#navPane { background:@bg; border-right:1px solid @stroke; }
QScrollArea { border:0; background:transparent; }
QScrollArea > QWidget > QWidget { background:transparent; }
QGroupBox { background:@surface; border:1px solid @stroke; border-radius:@rS; margin-top:16px; padding:16px 12px 12px 12px; }
QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 4px; color:@text2; font-weight:600; }

/* ---------- Butonlar: Standart / Accent / Subtle / Success / Danger ---------- */
QPushButton {
    background:@surface; color:@text; border:1px solid @strokeCtl; border-bottom-color:@strokeCtlBottom;
    border-radius:@rC; padding:7px 16px; font-weight:400; }
QPushButton:hover    { background:@hover; }
QPushButton:pressed  { background:@pressed; color:@text2; }
QPushButton:focus    { border:1px solid @brand; }
QPushButton:checked  { background:@brandTint; border:1px solid @brand; color:@brandPress; }
QPushButton:disabled { background:@disabledBg; color:@text3; border:1px solid @stroke; }

QPushButton[role="accent"], QPushButton#primary {
    background:@brand; color:#FFFFFF; border:1px solid @brand; font-weight:600; }
QPushButton[role="accent"]:hover,   QPushButton#primary:hover   { background:@brandHover; border-color:@brandHover; }
QPushButton[role="accent"]:pressed, QPushButton#primary:pressed { background:@brandPress; border-color:@brandPress; color:#E6E6E6; }
QPushButton[role="accent"]:disabled, QPushButton#primary:disabled { background:@strokeCtl; border-color:@strokeCtl; color:@text3; }

QPushButton[role="subtle"] { background:transparent; border:1px solid transparent; }
QPushButton[role="subtle"]:hover   { background:@hover2; }
QPushButton[role="subtle"]:pressed { background:@pressed; }

QPushButton[role="success"] { background:@success; color:#FFFFFF; border:1px solid @success; font-weight:600; }
QPushButton[role="success"]:hover   { background:@successHover; border-color:@successHover; }
QPushButton[role="success"]:pressed { background:@successPress; border-color:@successPress; }

QPushButton[role="danger"] { color:@danger; }
QPushButton[role="danger"]:hover   { background:@dangerBg; border-color:@dangerBorder; }
QPushButton[role="danger"]:pressed { background:@dangerBorder; }

QPushButton#toolbarAction { padding:6px 12px; }

/* ---------- Giriş alanları ---------- */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit, QDateTimeEdit, QTimeEdit, QTextEdit, QPlainTextEdit {
    background:@surface; color:@text; border:1px solid @strokeCtl; border-bottom:1px solid @strokeCtlBottom;
    border-radius:@rC; padding:6px 10px;
    selection-background-color:@brand; selection-color:#FFFFFF; }
QLineEdit:hover, QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QDateEdit:hover { background:@surfaceAlt; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QDateEdit:focus,
QTextEdit:focus, QPlainTextEdit:focus {
    background:@surface; border-bottom:2px solid @brand; padding-bottom:5px; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background:@disabledBg; color:@text3; }
QLineEdit#barcodeInput { border:1px solid @brand; border-bottom:2px solid @brand; background:@surface; font-weight:600; padding:8px 12px; }
QComboBox QAbstractItemView { background:@surface; border:1px solid @stroke; border-radius:@rC; padding:4px;
    selection-background-color:@hover2; selection-color:@text; outline:0; }

/* ---------- Tablo / liste ---------- */
QTableWidget, QTableView, QListWidget, QTreeWidget {
    background:@surface; alternate-background-color:@surfaceAlt; color:@text;
    border:1px solid @stroke; border-radius:@rS; gridline-color:@strokeSoft;
    selection-background-color:@brandTint; selection-color:@text; outline:0; }
QTableWidget::item, QTableView::item { padding:4px 8px; border:0; }
QTableWidget::item:hover, QTableView::item:hover, QListWidget::item:hover { background:@hover; }
QTableWidget::item:selected, QTableView::item:selected, QListWidget::item:selected { background:@brandTint; color:@text; }
QHeaderView { background:@surfaceAlt; border:0; }
QHeaderView::section { background:@surfaceAlt; color:@text2; border:0; border-bottom:1px solid @stroke;
    padding:10px 12px; font-weight:600; font-size:12px; }
QTableCornerButton::section { background:@surfaceAlt; border:0; border-bottom:1px solid @stroke; }

/* ---------- Gezinme (NavigationView) ---------- */
QListWidget#navigation { background:transparent; border:0; border-radius:0; color:@text; font-size:14px; outline:0; padding:4px 0; }
QListWidget#navigation::item { padding:10px 14px; margin:2px 8px; border-radius:@rC; border-left:3px solid transparent; }
QListWidget#navigation::item:hover    { background:@hover2; }
QListWidget#navigation::item:selected { background:@hover2; color:@text; border-left:3px solid @brand; font-weight:600; }

/* ---------- Sekmeler (Pivot) ---------- */
QTabWidget::pane { border:0; border-top:1px solid @stroke; background:transparent; padding:12px 0 0 0; top:-1px; }
QTabBar { background:transparent; }
QTabBar::tab { background:transparent; color:@text2; padding:10px 16px; margin-right:4px; border:0; border-bottom:2px solid transparent; }
QTabBar::tab:hover:!selected { color:@text; background:@hover2; border-top-left-radius:@rC; border-top-right-radius:@rC; }
QTabBar::tab:selected { color:@text; border-bottom:2px solid @brand; font-weight:600; }

/* ---------- Seçim kutuları ---------- */
QCheckBox, QRadioButton { spacing:8px; background:transparent; }
QCheckBox:disabled, QRadioButton:disabled { color:@text3; }

/* ---------- Menü, ipucu, durum çubuğu ---------- */
QMenu { background:@surface; border:1px solid @stroke; border-radius:@rS; padding:4px; }
QMenu::item { padding:8px 24px 8px 16px; margin:1px 2px; border-radius:@rC; }
QMenu::item:selected { background:@hover2; color:@text; }
QMenu::separator { height:1px; background:@stroke; margin:4px 8px; }
QToolTip { background:@surface; color:@text; border:1px solid @stroke; padding:6px 8px; }
QStatusBar { background:@bg; color:@text2; border-top:1px solid @stroke; }
QStatusBar::item { border:0; }
QProgressBar { background:@strokeSoft; border:0; border-radius:2px; height:4px; text-align:center; color:transparent; }
QProgressBar::chunk { background:@brand; border-radius:2px; }

/* ---------- İnce kaydırma çubukları ---------- */
QScrollBar:vertical   { background:transparent; width:12px; margin:0; }
QScrollBar:horizontal { background:transparent; height:12px; margin:0; }
QScrollBar::handle:vertical   { background:#C2C2C2; border-radius:3px; min-height:32px; margin:2px 3px; }
QScrollBar::handle:horizontal { background:#C2C2C2; border-radius:3px; min-width:32px;  margin:3px 2px; }
QScrollBar::handle:hover { background:#8A8A8A; }
QScrollBar::add-line, QScrollBar::sub-line { width:0; height:0; background:none; border:0; }
QScrollBar::add-page, QScrollBar::sub-page { background:none; }
)QSS");

    // Token tablosu (Fluent 2 – açık tema)
    const QList<QPair<QString, QString>> tokens = {
        {"@bg",              "#F3F3F3"},  // Mica zemin
        {"@surface",         "#FFFFFF"},
        {"@surfaceAlt",      "#FAFAFA"},
        {"@text",            "#242424"},
        {"@text2",           "#616161"},
        {"@text3",           "#A3A3A3"},
        {"@stroke",          "#E0E0E0"},
        {"@strokeSoft",      "#EDEDED"},
        {"@strokeCtl",       "#D1D1D1"},
        {"@strokeCtlBottom", "#8A8A8A"},
        {"@hover",           "#F5F5F5"},
        {"@hover2",          "#EBEBEB"},
        {"@pressed",         "#E0E0E0"},
        {"@disabledBg",      "#F0F0F0"},
        {"@brandHover",      "#115EA3"},
        {"@brandPress",      "#0F548C"},
        {"@brandTint2",      "#CFE4FA"},
        {"@brandTint",       "#EBF3FC"},
        {"@brand",           "#0F6CBD"},
        {"@successHover",    "#0E700E"},
        {"@successPress",    "#0C5E0C"},
        {"@success",         "#107C10"},
        {"@dangerBorder",    "#F1BBBC"},
        {"@dangerBg",        "#FDF3F4"},
        {"@danger",          "#C50F1F"},
        {"@rC",              QString::number(kRadiusControl) + "px"},
        {"@rS",              QString::number(kRadiusSurface) + "px"},
    };
    for (const auto &t : tokens) s.replace(t.first, t.second);
    return s;
}

inline void apply(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));   // her Windows sürümünde aynı zemin

    QFont f;
    f.setFamilies({"Segoe UI Variable Text", "Segoe UI"});
    f.setPixelSize(14);
    app.setFont(f);

    QPalette p;
    p.setColor(QPalette::Window,          QColor("#F3F3F3"));
    p.setColor(QPalette::WindowText,      QColor("#242424"));
    p.setColor(QPalette::Base,            QColor("#FFFFFF"));
    p.setColor(QPalette::AlternateBase,   QColor("#FAFAFA"));
    p.setColor(QPalette::Text,            QColor("#242424"));
    p.setColor(QPalette::Button,          QColor("#FFFFFF"));
    p.setColor(QPalette::ButtonText,      QColor("#242424"));
    p.setColor(QPalette::Highlight,       QColor("#0F6CBD"));
    p.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    p.setColor(QPalette::ToolTipBase,     QColor("#FFFFFF"));
    p.setColor(QPalette::ToolTipText,     QColor("#242424"));
    p.setColor(QPalette::PlaceholderText, QColor("#8A8A8A"));
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor("#A3A3A3"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#A3A3A3"));
    app.setPalette(p);

    app.setStyleSheet(qss());
}

// Ürün kutusu (POS ızgarası) — kart + hover vurgusu
inline QString tileQss() {
    return QStringLiteral(
        "QPushButton{background:#FFFFFF;border:1px solid #E0E0E0;border-radius:8px;padding:0;text-align:center;}"
        "QPushButton:hover{background:#FAFAFA;border:1px solid #0F6CBD;}"
        "QPushButton:pressed{background:#EBF3FC;}"
        "QPushButton:disabled{background:#F5F5F5;border:1px solid #EDEDED;color:#A3A3A3;}");
}

} // namespace FluentDesign
