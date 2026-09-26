#ifndef ASE_EDITOR_ACCESSIBLE_H
#define ASE_EDITOR_ACCESSIBLE_H

#include <QAccessibleWidget>

class EditorViewport;

/*
 * What a screen reader sees when it reaches the editor — see
 * docs/adr/0146.
 *
 * The viewport paints its own text, so nothing about it is discoverable
 * the way a QTextEdit's is: without this the editing area is an opaque
 * rectangle with a name and no contents.
 */
class EditorAccessible : public QAccessibleWidget, public QAccessibleTextInterface {
public:
    explicit EditorAccessible(EditorViewport *viewport);

    void *interface_cast(QAccessible::InterfaceType type) override;
    QAccessible::State state() const override;
    QString text(QAccessible::Text which) const override;

    /* QAccessibleTextInterface */
    void selection(int selectionIndex, int *startOffset, int *endOffset) const override;
    int selectionCount() const override;
    void addSelection(int startOffset, int endOffset) override;
    void removeSelection(int selectionIndex) override;
    void setSelection(int selectionIndex, int startOffset, int endOffset) override;
    int cursorPosition() const override;
    void setCursorPosition(int position) override;
    QString text(int startOffset, int endOffset) const override;
    QString textBeforeOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                              int *startOffset, int *endOffset) const override;
    QString textAfterOffset(int offset, QAccessible::TextBoundaryType boundaryType,
                             int *startOffset, int *endOffset) const override;
    QString textAtOffset(int offset, QAccessible::TextBoundaryType boundaryType, int *startOffset,
                          int *endOffset) const override;
    int characterCount() const override;
    QRect characterRect(int offset) const override;
    int offsetAtPoint(const QPoint &point) const override;
    void scrollToSubstring(int startIndex, int endIndex) override;
    QString attributes(int offset, int *startOffset, int *endOffset) const override;

private:
    EditorViewport *editor() const;
    /* The unit of `boundaryType` containing `offset`, as a half-open
     * range. Every one of the three text* functions above is this plus
     * a step in one direction. */
    void boundsAt(int offset, QAccessible::TextBoundaryType boundaryType, int *start,
                   int *end) const;
};

/* Installed once, from the first EditorViewport constructed. */
void installEditorAccessibility();

#endif /* ASE_EDITOR_ACCESSIBLE_H */
