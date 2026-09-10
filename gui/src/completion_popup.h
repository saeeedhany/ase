#ifndef ASE_COMPLETION_POPUP_H
#define ASE_COMPLETION_POPUP_H

#include "tracking_popup.h"

#include <QColor>
#include <QPoint>
#include <QVector>

class EditorViewport;

/*
 * The automatic completion dropdown — see docs/adr/0030, docs/adr/0031.
 * A TrackingPopup: fades in once, then glides its position (not its
 * size) to follow the caret as the user keeps typing, instead of
 * jumping on every keystroke. Custom-painted rows, not QListWidget —
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
class CompletionPopup : public TrackingPopup {
public:
    struct Item {
        QString label;
        QString insertText; /* what actually gets inserted; may equal label */
        QString detail;     /* optional trailing type/signature hint, dimmed */
    };

    explicit CompletionPopup(EditorViewport *viewport);

    /* Replaces the item list and retargets the popup with its top-left
     * anchored at `pos` (viewport-local pixel coordinates, e.g. just
     * below the caret) — see TrackingPopup::retarget. An empty `items`
     * hides the popup instead of showing an empty box. */
    void showItems(const QVector<Item> &items, const QPoint &pos);
    bool isShowingPopup() const { return isTrackingVisible() && !m_items.isEmpty(); }

    void moveSelection(int delta);
    /* Null if there are no items — always check isShowingPopup() first. */
    const Item *selectedItem() const;

    void refreshTheme() override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QSize contentSize() const;
    void ensureSelectionVisible();

    QVector<Item> m_items;
    int m_selected = 0;
    int m_scrollOffset = 0; /* index of the first visible row */

    QColor m_textColor;
    QColor m_dimColor;
    QColor m_selectionColor;
};

#endif /* ASE_COMPLETION_POPUP_H */
