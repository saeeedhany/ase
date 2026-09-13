#ifndef ASE_FIND_BAR_H
#define ASE_FIND_BAR_H

#include "floating_panel.h"

class SmoothLineEdit;
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
    /* Project rides in this panel as a third mode for the same reason
     * QuickOpen rides in the file browser (docs/adr/0065): it is the
     * same badge, field, theming and Escape handling, and the only
     * difference is what Enter does with what you typed. See
     * docs/adr/0066. */
    enum class Mode { Find, Replace, Project };

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
    void restoreFocusAfterDrag() override;

private:
    EditorViewport *m_viewport;
    Mode m_mode = Mode::Find;
    SmoothLineEdit *m_findEdit;
    SmoothLineEdit *m_replaceEdit;
    QWidget *m_replaceRow;
    LetterBadge *m_findBadge;
    LetterBadge *m_replaceBadge;
};

#endif /* ASE_FIND_BAR_H */
