/*
 * The editor as an accessibility tree node — see docs/adr/0146.
 *
 * Everything here converts between the byte offsets the editor works in
 * and the QString indices the accessibility layer speaks. They differ
 * the moment a file stops being ASCII, and a screen reader reading the
 * wrong character is worse than one reading nothing.
 */
#include "editor_viewport.h"

#include <QAccessible>
#include <QRect>

#include <algorithm>

QString EditorViewport::a11yText() const {
    return QString::fromUtf8(m_cache);
}

int EditorViewport::a11yCharacterCount() const {
    return a11yText().size();
}

/* The two conversions, kept together so they cannot drift apart. */
static int indexForByte(const QByteArray &cache, size_t byteOffset) {
    int clamped = std::min(static_cast<int>(byteOffset), static_cast<int>(cache.size()));
    return QString::fromUtf8(cache.constData(), clamped).size();
}

static size_t byteForIndex(const QByteArray &cache, int index) {
    const QString text = QString::fromUtf8(cache);
    int clamped = std::clamp(index, 0, static_cast<int>(text.size()));
    return static_cast<size_t>(QStringView(text).left(clamped).toUtf8().size());
}

int EditorViewport::a11yCursorPosition() const {
    return indexForByte(m_cache, cursorOffset());
}

void EditorViewport::setA11yCursorPosition(int index) {
    size_t offset = byteForIndex(m_cache, index);
    collapseToOneCursor();
    m_cursors[0] = offset;
    m_selectionAnchors[0] = offset;
    ensureCursorVisible();
    update();
}

bool EditorViewport::a11ySelection(int *start, int *end) const {
    size_t from = 0;
    size_t to = 0;
    if (!primarySelection(&from, &to)) {
        return false;
    }
    *start = indexForByte(m_cache, from);
    *end = indexForByte(m_cache, to);
    return true;
}

void EditorViewport::setA11ySelection(int start, int end) {
    size_t from = byteForIndex(m_cache, std::min(start, end));
    size_t to = byteForIndex(m_cache, std::max(start, end));
    collapseToOneCursor();
    m_selectionAnchors[0] = from;
    m_cursors[0] = to;
    ensureCursorVisible();
    update();
}

void EditorViewport::a11yLineAt(int index, int *start, int *end) const {
    size_t offset = byteForIndex(m_cache, index);
    int line = lineForOffset(offset);
    *start = indexForByte(m_cache, static_cast<size_t>(m_lineStarts[line]));
    /* The line's text, without its newline: a screen reader reading a
     * line should not announce the break as part of it. */
    size_t lineEnd = (line + 1 < m_lineStarts.size())
                         ? static_cast<size_t>(m_lineStarts[line + 1] - 1)
                         : static_cast<size_t>(m_cache.size());
    *end = indexForByte(m_cache, lineEnd);
}

/* In this widget's coordinates, which is what QAccessible expects after
 * it maps them to the screen. */
QRect EditorViewport::a11yCharacterRect(int index) const {
    size_t offset = byteForIndex(m_cache, index);
    int line = lineForOffset(offset);
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1
                                                    : static_cast<int>(m_cache.size());
    int column = columnForOffset(offset, line);
    int x0 = xForColumn(lineStart, lineEnd, column);
    int x1 = xForColumn(lineStart, lineEnd, column + 1);
    int y = static_cast<int>((line - m_renderedScrollLine) * m_lineHeight);
    return QRect(gutterWidth() + x0 - static_cast<int>(m_renderedScrollX), y, std::max(1, x1 - x0),
                  m_lineHeight);
}

int EditorViewport::a11yOffsetAtPoint(const QPoint &point) const {
    return indexForByte(m_cache, offsetForPoint(point));
}

/*
 * A reader that is only queried reads a stale snapshot: it has to be
 * told. Nothing here runs unless an assistive technology is attached,
 * which is what QAccessible::isActive() answers — so the diff below
 * costs nothing in the normal case.
 */
void EditorViewport::notifyAccessibleTextChange(const QByteArray &before) {
    if (!QAccessible::isActive() || before == m_cache) {
        return;
    }
    const QString oldText = QString::fromUtf8(before);
    const QString newText = QString::fromUtf8(m_cache);

    /* The smallest edit that explains the difference. A keystroke is one
     * character in the middle of a file, and saying so is what lets a
     * reader announce the character rather than re-read the document. */
    int prefix = 0;
    while (prefix < oldText.size() && prefix < newText.size() && oldText[prefix] == newText[prefix]) {
        prefix++;
    }
    int suffix = 0;
    while (suffix < oldText.size() - prefix && suffix < newText.size() - prefix &&
           oldText[oldText.size() - 1 - suffix] == newText[newText.size() - 1 - suffix]) {
        suffix++;
    }
    const QString removed = oldText.mid(prefix, oldText.size() - prefix - suffix);
    const QString inserted = newText.mid(prefix, newText.size() - prefix - suffix);

    if (!removed.isEmpty() && !inserted.isEmpty()) {
        QAccessibleTextUpdateEvent event(this, prefix, removed, inserted);
        QAccessible::updateAccessibility(&event);
    } else if (!inserted.isEmpty()) {
        QAccessibleTextInsertEvent event(this, prefix, inserted);
        QAccessible::updateAccessibility(&event);
    } else if (!removed.isEmpty()) {
        QAccessibleTextRemoveEvent event(this, prefix, removed);
        QAccessible::updateAccessibility(&event);
    }
}

void EditorViewport::notifyAccessibleCursor() {
    if (!QAccessible::isActive()) {
        return;
    }
    int position = a11yCursorPosition();
    if (position == m_lastAnnouncedCursor) {
        return;
    }
    m_lastAnnouncedCursor = position;
    QAccessibleTextCursorEvent event(this, position);
    QAccessible::updateAccessibility(&event);
}
