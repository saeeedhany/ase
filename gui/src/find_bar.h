#ifndef ASE_FIND_BAR_H
#define ASE_FIND_BAR_H

#include <QWidget>

class QLineEdit;
class EditorViewport;

/*
 * Thin, keyboard-only bar docked above the editor viewport (see
 * gui/src/main.cpp's central-widget layout) — Ctrl+F/Ctrl+H open it,
 * Escape closes it. Owns only the input widgets; all search/replace
 * logic (matching, highlighting, undo-grouped replace) lives on
 * EditorViewport, reached through the plain pointer set by
 * EditorViewport::setFindBar. See docs/adr/0021.
 */
class FindBar : public QWidget {
public:
    enum class Mode { Find, Replace };

    explicit FindBar(EditorViewport *viewport, QWidget *parent = nullptr);

    /* Shows the bar (revealing the replace field too, in Replace mode),
     * pre-fills the find field from the current single-line selection
     * (if any — common editor convention), focuses it, and re-runs the
     * query against the viewport so reopening after an edit shows
     * fresh matches. */
    void openFor(Mode mode);
    void hideBar();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    EditorViewport *m_viewport;
    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QWidget *m_replaceRow;
};

#endif /* ASE_FIND_BAR_H */
