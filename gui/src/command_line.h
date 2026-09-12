#ifndef ASE_COMMAND_LINE_H
#define ASE_COMMAND_LINE_H

#include "floating_panel.h"

class SmoothLineEdit;
class EditorViewport;
class LetterBadge;

/*
 * The `:` command line as a centered floating panel, same family as
 * FindBar/FileBrowserPanel — see docs/adr/0022 (the design system) and
 * docs/adr/0025 (this panel). One ":" badge, one field. Enter runs the
 * typed command via EditorViewport::runCommand and closes; Escape
 * cancels without running anything. No buttons, no command history —
 * v1 keeps this as small as FindBar.
 */
class CommandLine : public FloatingPanel {
public:
    explicit CommandLine(EditorViewport *viewport);

    void openCommandLine();
    void refreshTheme();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void restoreFocusAfterDrag() override;

private:
    void hideBar();

    EditorViewport *m_viewport;
    LetterBadge *m_badge;
    SmoothLineEdit *m_edit;
};

#endif /* ASE_COMMAND_LINE_H */
