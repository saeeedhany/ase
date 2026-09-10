#ifndef ASE_SCROLLBAR_STYLE_H
#define ASE_SCROLLBAR_STYLE_H

#include <QColor>
#include <QString>

/* A thin scrollbar (6px), thickening slightly (10px) on hover — shared
 * by every scrollable panel (Help, the output panel, the file
 * browser's list). QPalette has no equivalent for scrollbar width or
 * hover state, so this is the one deliberate stylesheet in a codebase
 * that otherwise paints everything itself via QPainter — scoped
 * strictly to QScrollBar, not a general styling escape hatch. See
 * docs/adr/0027. Both colors are pushed in by the caller (from
 * EditorViewport's theme accessors), same "no config/theme access of
 * its own" shape every other bit of shared chrome in this app follows. */
QString thinScrollBarStyleSheet(const QColor &handleColor, const QColor &handleHoverColor);

#endif /* ASE_SCROLLBAR_STYLE_H */
