#pragma once
#include <QtWidgets>

// Centralized UI metrics. No database or business logic lives here.
namespace UiDesign {
inline constexpr int kTouchTarget = 44;
inline constexpr int kSidebarWidth = 220;
inline constexpr int kCardMinWidth = 128;
inline constexpr int kCardMaxWidth = 156;

inline void polishTable(QTableWidget *table) {
    if (!table) return;
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(42);
    table->horizontalHeader()->setMinimumSectionSize(76);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
}

inline void polishAction(QPushButton *button) {
    if (!button) return;
    button->setMinimumHeight(kTouchTarget);
    button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    button->setToolTip(button->text());
}

inline void polishDialog(QDialog *dialog) {
    if (!dialog) return;
    dialog->setSizeGripEnabled(true);
    dialog->setMinimumSize(480, 340);
    dialog->setWindowFlag(Qt::WindowMinMaxButtonsHint, true);
}

inline QString globalQss() {
    return QString::fromUtf8(R"QSS(
      QToolTip { background:#18273D; color:white; border:0; padding:8px; }
      QScrollBar:vertical { width:11px; background:#F2F5F8; }
      QScrollBar::handle:vertical { background:#BECAD6; border-radius:5px; min-height:28px; }
      QScrollBar:horizontal { height:11px; background:#F2F5F8; }
      QScrollBar::handle:horizontal { background:#BECAD6; border-radius:5px; min-width:28px; }
      QPushButton:focus { border:2px solid #F15A32; }
      QTableWidget { gridline-color:#EBF0F5; }
    )QSS");
}
} // namespace UiDesign
