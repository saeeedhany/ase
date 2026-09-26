#include "editor_accessible.h"

#include "editor_viewport.h"

#include <QFileInfo>
#include <QTextBoundaryFinder>

#include <algorithm>

EditorAccessible::EditorAccessible(EditorViewport *viewport)
    : QAccessibleWidget(viewport, QAccessible::EditableText) {}

EditorViewport *EditorAccessible::editor() const {
    return qobject_cast<EditorViewport *>(object());
}

void *EditorAccessible::interface_cast(QAccessible::InterfaceType type) {
    if (type == QAccessible::TextInterface) {
        return static_cast<QAccessibleTextInterface *>(this);
    }
    return QAccessibleWidget::interface_cast(type);
}

QAccessible::State EditorAccessible::state() const {
    QAccessible::State s = QAccessibleWidget::state();
    s.editable = true;
    s.multiLine = true;
    s.selectableText = true;
    s.focusable = true;
    return s;
}

QString EditorAccessible::text(QAccessible::Text which) const {
    EditorViewport *view = editor();
    if (view == nullptr) {
        return QString();
    }
    switch (which) {
    case QAccessible::Value:
        return view->a11yText();
    case QAccessible::Name: {
        /* The file, so moving between buffers is audible. An unnamed
         * buffer says so rather than announcing an empty string. */
        const QString path = view->filePath();
        return path.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(path).fileName();
    }
    case QAccessible::Description:
    default:
        return QAccessibleWidget::text(which);
    }
}

int EditorAccessible::characterCount() const {
    EditorViewport *view = editor();
    return view != nullptr ? view->a11yCharacterCount() : 0;
}

int EditorAccessible::cursorPosition() const {
    EditorViewport *view = editor();
    return view != nullptr ? view->a11yCursorPosition() : 0;
}

void EditorAccessible::setCursorPosition(int position) {
    if (EditorViewport *view = editor()) {
        view->setA11yCursorPosition(position);
    }
}

QString EditorAccessible::text(int startOffset, int endOffset) const {
    EditorViewport *view = editor();
    if (view == nullptr || startOffset > endOffset) {
        return QString();
    }
    const QString all = view->a11yText();
    int from = std::clamp(startOffset, 0, static_cast<int>(all.size()));
    int to = std::clamp(endOffset, from, static_cast<int>(all.size()));
    return all.mid(from, to - from);
}

int EditorAccessible::selectionCount() const {
    EditorViewport *view = editor();
    int start = 0;
    int end = 0;
    return (view != nullptr && view->a11ySelection(&start, &end)) ? 1 : 0;
}

void EditorAccessible::selection(int selectionIndex, int *startOffset, int *endOffset) const {
    *startOffset = 0;
    *endOffset = 0;
    EditorViewport *view = editor();
    if (view == nullptr || selectionIndex != 0) {
        return;
    }
    view->a11ySelection(startOffset, endOffset);
}

void EditorAccessible::addSelection(int startOffset, int endOffset) {
    setSelection(0, startOffset, endOffset);
}

void EditorAccessible::removeSelection(int selectionIndex) {
    if (selectionIndex != 0) {
        return;
    }
    /* Collapsing to the caret is what "no selection" means here; there
     * is no separate unselected state to return to. */
    if (EditorViewport *view = editor()) {
        view->setA11yCursorPosition(view->a11yCursorPosition());
    }
}

void EditorAccessible::setSelection(int selectionIndex, int startOffset, int endOffset) {
    if (selectionIndex != 0) {
        return;
    }
    if (EditorViewport *view = editor()) {
        view->setA11ySelection(startOffset, endOffset);
    }
}

QRect EditorAccessible::characterRect(int offset) const {
    EditorViewport *view = editor();
    if (view == nullptr) {
        return QRect();
    }
    /* Screen coordinates: the interface is asked where something is on
     * the display, not inside this widget. */
    const QRect local = view->a11yCharacterRect(offset);
    return QRect(view->mapToGlobal(local.topLeft()), local.size());
}

int EditorAccessible::offsetAtPoint(const QPoint &point) const {
    EditorViewport *view = editor();
    if (view == nullptr) {
        return -1;
    }
    return view->a11yOffsetAtPoint(view->mapFromGlobal(point));
}

void EditorAccessible::scrollToSubstring(int startIndex, int endIndex) {
    Q_UNUSED(endIndex);
    if (EditorViewport *view = editor()) {
        view->setA11yCursorPosition(startIndex);
    }
}

QString EditorAccessible::attributes(int offset, int *startOffset, int *endOffset) const {
    /* One run, no attributes: the editor draws syntax colour but
     * exposes no per-character formatting a reader could use. */
    *startOffset = offset;
    *endOffset = offset + 1;
    return QString();
}

/*
 * A word is what QTextBoundaryFinder says it is rather than what the
 * editor's own isWordChar does: a reader announcing words should match
 * the platform's idea of one, and vim's is deliberately narrower.
 */
void EditorAccessible::boundsAt(int offset, QAccessible::TextBoundaryType boundaryType, int *start,
                                 int *end) const {
    const QString all = editor() != nullptr ? editor()->a11yText() : QString();
    const int size = static_cast<int>(all.size());
    int at = std::clamp(offset, 0, size);

    switch (boundaryType) {
    case QAccessible::CharBoundary:
        *start = at;
        *end = std::min(at + 1, size);
        return;
    case QAccessible::LineBoundary:
    case QAccessible::ParagraphBoundary:
        if (EditorViewport *view = editor()) {
            view->a11yLineAt(at, start, end);
        } else {
            *start = 0;
            *end = 0;
        }
        return;
    case QAccessible::WordBoundary:
    case QAccessible::SentenceBoundary: {
        QTextBoundaryFinder::BoundaryType type = (boundaryType == QAccessible::WordBoundary)
                                                      ? QTextBoundaryFinder::Word
                                                      : QTextBoundaryFinder::Sentence;
        QTextBoundaryFinder finder(type, all);
        finder.setPosition(at);
        *start = finder.isAtBoundary() ? at : finder.toPreviousBoundary();
        if (*start < 0) {
            *start = 0;
        }
        finder.setPosition(*start);
        int next = finder.toNextBoundary();
        *end = (next < 0) ? size : next;
        return;
    }
    case QAccessible::NoBoundary:
    default:
        *start = 0;
        *end = size;
        return;
    }
}

QString EditorAccessible::textAtOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                                        int *startOffset, int *endOffset) const {
    /* -1 means "where the caret is", which is what a reader asks when it
     * wants the line you are on. */
    int at = (offset < 0) ? cursorPosition() : offset;
    boundsAt(at, boundaryType, startOffset, endOffset);
    return text(*startOffset, *endOffset);
}

QString EditorAccessible::textBeforeOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                                            int *startOffset, int *endOffset) const {
    int at = (offset < 0) ? cursorPosition() : offset;
    int start = 0;
    int end = 0;
    boundsAt(at, boundaryType, &start, &end);
    if (start <= 0) {
        *startOffset = -1;
        *endOffset = -1;
        return QString();
    }
    boundsAt(start - 1, boundaryType, startOffset, endOffset);
    return text(*startOffset, *endOffset);
}

QString EditorAccessible::textAfterOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                                           int *startOffset, int *endOffset) const {
    int at = (offset < 0) ? cursorPosition() : offset;
    int start = 0;
    int end = 0;
    boundsAt(at, boundaryType, &start, &end);
    if (end >= characterCount()) {
        *startOffset = -1;
        *endOffset = -1;
        return QString();
    }
    /* A line's end is the newline's own offset, and the newline still
     * belongs to the line before it — so landing on `end` asks about
     * the unit we just came from. A word's end is already the next
     * word's start, so stepping again there would skip one. Which of
     * the two this is, is a question worth asking rather than
     * assuming. */
    boundsAt(end, boundaryType, startOffset, endOffset);
    if (*startOffset == start) {
        boundsAt(end + 1, boundaryType, startOffset, endOffset);
    }
    return text(*startOffset, *endOffset);
}

namespace {

QAccessibleInterface *editorAccessibleFactory(const QString &className, QObject *object) {
    if (className == QLatin1String("EditorViewport") && object != nullptr && object->isWidgetType()) {
        return new EditorAccessible(static_cast<EditorViewport *>(object));
    }
    return nullptr;
}

} // namespace

void installEditorAccessibility() {
    static bool installed = [] {
        QAccessible::installFactory(editorAccessibleFactory);
        return true;
    }();
    Q_UNUSED(installed);
}
