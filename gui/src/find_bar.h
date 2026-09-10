#ifndef ASE_FIND_BAR_H
#define ASE_FIND_BAR_H

#include "floating_panel.h"

class QLineEdit;
class EditorViewport;
class LetterBadge;

/*
 * Find/replace as a centered floating panel — see docs/adr/0022. Owns
 * only the input widgets and their key handling; all search/replace
 * logic (matching, highlighting, undo-grouped replace) lives on
 * EditorViewport, reached through the plain pointer set at
 * construction. Ctrl+F/Ctrl+H open it, Escape closes it. No buttons —
 * every action is a keybinding (see find_bar.cpp's eventFilter).
 */
class FindBar : public FloatingPanel {
public:
    enum class Mode { Find, Replace };

    /* `viewport` is both the logic owner and the host FloatingPanel
     * centers over. */
    explicit FindBar(EditorViewport *viewport);

    void openFor(Mode mode);
    void hideBar();
    /* Re-pulls colors from EditorViewport — called on every config
     * hot-reload (see EditorViewport::checkConfigReload) so an edited
     * config.ase takes effect on this panel even while it's open. */
    void refreshTheme();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    EditorViewport *m_viewport;
    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QWidget *m_replaceRow;
    LetterBadge *m_findBadge;
    LetterBadge *m_replaceBadge;
};

#endif /* ASE_FIND_BAR_H */
