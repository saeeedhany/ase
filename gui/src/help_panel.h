#ifndef ASE_HELP_PANEL_H
#define ASE_HELP_PANEL_H

#include "floating_panel.h"

class QLabel;
class QScrollArea;
class EditorViewport;
class LetterBadge;

/*
 * Keybinding reference as a centered floating panel, same family as
 * the rest — see docs/adr/0022 (the design system) and docs/adr/0026
 * (this panel, added directly in response to user feedback once the
 * keybinding scheme itself settled — this panel's whole reason to
 * exist is documenting that scheme). "?" badge, `Ctrl+/` opens it.
 * Pure display, no input field: a scrollable rich-text body listing
 * every binding by category, Escape closes it. The body text is
 * maintained by hand here rather than generated from the keybinding
 * dispatch code — the two can drift; that's an accepted v1 tradeoff
 * (a generated single source of truth is real, unscoped work).
 */
class HelpPanel : public FloatingPanel {
public:
    explicit HelpPanel(EditorViewport *viewport);

    void openHelp();
    void refreshTheme();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void hideBar();

    EditorViewport *m_viewport;
    LetterBadge *m_badge;
    QLabel *m_title;
    QLabel *m_body;
    QScrollArea *m_scrollArea;
};

#endif /* ASE_HELP_PANEL_H */
