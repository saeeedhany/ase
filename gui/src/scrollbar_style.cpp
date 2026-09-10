#include "scrollbar_style.h"

QString thinScrollBarStyleSheet(const QColor &handleColor, const QColor &handleHoverColor) {
    return QStringLiteral("QScrollBar:vertical { background: transparent; width: 6px; margin: 0px; }"
                           "QScrollBar:vertical:hover { width: 10px; }"
                           "QScrollBar::handle:vertical { background: %1; min-height: 24px; border-radius: 3px; }"
                           "QScrollBar::handle:vertical:hover { background: %2; border-radius: 5px; }"
                           "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
                           "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
                           "QScrollBar:horizontal { background: transparent; height: 6px; margin: 0px; }"
                           "QScrollBar:horizontal:hover { height: 10px; }"
                           "QScrollBar::handle:horizontal { background: %1; min-width: 24px; border-radius: 3px; }"
                           "QScrollBar::handle:horizontal:hover { background: %2; border-radius: 5px; }"
                           "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
                           "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }")
        .arg(handleColor.name(QColor::HexArgb), handleHoverColor.name(QColor::HexArgb));
}
