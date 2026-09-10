#ifndef ASE_HOVER_PANEL_H
#define ASE_HOVER_PANEL_H

#include <QColor>
#include <QPoint>
#include <QString>
#include <QWidget>

class QLabel;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class EditorViewport;

/*
 * The mouse-hover info tooltip (textDocument/hover) — see
 * docs/adr/0030. Same reasoning as CompletionPopup for not being a
 * FloatingPanel: it tracks the pointer and has to appear/disappear
 * quickly and often, not glance-act-dismiss like Find/Replace. Purely
 * informational — WA_TransparentForMouseEvents, never focusable, never
 * intercepts a click. EditorViewport owns all the timing (the pause-
 * before-request delay, dismiss-on-move) via its own QTimer; this
 * class just renders whatever text it's given at a given position.
 */
class HoverPanel : public QWidget {
public:
    explicit HoverPanel(EditorViewport *viewport);

    /* Shows `text` (plain text — v1 doesn't render markdown, see
     * docs/adr/0030) in a word-wrapped box anchored near `pos`
     * (viewport-local pixel coordinates, typically just above the
     * mouse), clamped to stay inside the viewport. Empty `text` is a
     * no-op-safe way to say "nothing to show" — callers should check
     * before calling, but this won't misbehave if they don't. */
    void showText(const QString &text, const QPoint &pos);
    void dismiss();
    bool isShowingHover() const { return isVisible(); }

    void refreshTheme();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    EditorViewport *m_viewport;
    QLabel *m_label;
    QColor m_background;
    QColor m_border;

    QGraphicsOpacityEffect *m_opacityEffect;
    QPropertyAnimation *m_fadeAnimation;
};

#endif /* ASE_HOVER_PANEL_H */
