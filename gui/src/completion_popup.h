#ifndef ASE_COMPLETION_POPUP_H
#define ASE_COMPLETION_POPUP_H

#include <QColor>
#include <QPoint>
#include <QVector>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class EditorViewport;

/*
 * The automatic completion dropdown — see docs/adr/0030. Unlike
 * FindBar/HelpPanel/etc (FloatingPanel: centered over the whole host,
 * a deliberate glance-act-dismiss window with a scale+fade "pop"), this
 * has to track the caret and refresh its contents on nearly every
 * keystroke while the user is simply typing. A scale animation replayed
 * that often would read as busy rather than smooth, so this only fades
 * (and only on the hidden->visible transition, never on a same-still-
 * open content refresh) and positions itself with a plain move(), no
 * host-relative anchor system. Custom-painted rows, not QListWidget —
 * this project already hit real geometry-timing bugs with QListWidget
 * (FileBrowserPanel, see docs/adr/0024) and this widget resizes far
 * more often than that one ever did.
 *
 * Never takes focus and has no event handling of its own — EditorViewport
 * intercepts Up/Down/Enter/Tab/Escape itself (see its keyPressEvent) and
 * calls back into this class's plain methods; typing a further character
 * still goes to EditorViewport's normal insert path, which naturally
 * retriggers a fresh completion request and a fresh showItems() call.
 */
class CompletionPopup : public QWidget {
public:
    struct Item {
        QString label;
        QString insertText; /* what actually gets inserted; may equal label */
        QString detail;     /* optional trailing type/signature hint, dimmed */
    };

    explicit CompletionPopup(EditorViewport *viewport);

    /* Replaces the item list and (re)shows the popup anchored with its
     * top-left at `pos` (viewport-local pixel coordinates, e.g. just
     * below the caret) — clamped to stay fully inside the viewport,
     * flipping above `pos` instead of below when there isn't room. An
     * empty `items` hides the popup instead of showing an empty box.
     * Fades in only on the hidden->visible edge; an already-open popup
     * just updates content and geometry instantly, so continued typing
     * doesn't replay the fade on every keystroke. */
    void showItems(const QVector<Item> &items, const QPoint &pos);
    void dismiss();
    bool isShowingPopup() const { return isVisible() && !m_items.isEmpty(); }

    void moveSelection(int delta);
    /* Null if there are no items — always check isShowingPopup() first. */
    const Item *selectedItem() const;

    void refreshTheme();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QSize contentSize() const;
    void ensureSelectionVisible();

    EditorViewport *m_viewport;
    QVector<Item> m_items;
    int m_selected = 0;
    int m_scrollOffset = 0; /* index of the first visible row */

    QColor m_background;
    QColor m_border;
    QColor m_textColor;
    QColor m_dimColor;
    QColor m_selectionColor;

    QGraphicsOpacityEffect *m_opacityEffect;
    QPropertyAnimation *m_fadeAnimation;
};

#endif /* ASE_COMPLETION_POPUP_H */
