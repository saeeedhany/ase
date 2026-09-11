#ifndef ASE_ABOUT_PANEL_H
#define ASE_ABOUT_PANEL_H

#include "floating_panel.h"

class QLabel;
class EditorViewport;
class LetterBadge;

/*
 * App info as a centered floating panel, same family as the rest —
 * see docs/adr/0022 (the design system), docs/adr/0026 (this panel),
 * and docs/adr/0027 (the logo). "i" badge, `Ctrl+I` opens it. Pure
 * display: the app's logo (gui/resources/ase.png, bundled via Qt
 * resources — see gui/resources/resources.qrc), name, version, author,
 * a couple of current-state notes, and the author's GitHub/website
 * links (clickable — QLabel's rich-text links, opened via the system
 * browser). Escape closes it.
 */
class AboutPanel : public FloatingPanel {
public:
    explicit AboutPanel(EditorViewport *viewport);

    void openAbout();
    void refreshTheme();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void restoreFocusAfterDrag() override;

private:
    void hideBar();

    EditorViewport *m_viewport;
    LetterBadge *m_badge;
    QLabel *m_title;
    QLabel *m_logo;
    QLabel *m_body;
};

#endif /* ASE_ABOUT_PANEL_H */
