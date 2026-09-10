#ifndef ASE_HOVER_PANEL_H
#define ASE_HOVER_PANEL_H

#include "tracking_popup.h"

#include <QPoint>
#include <QString>

class QLabel;
class EditorViewport;

/*
 * The mouse-hover info tooltip (textDocument/hover) — see
 * docs/adr/0030, docs/adr/0031. A TrackingPopup: fades in once when a
 * hover response first arrives, then glides its position to keep
 * tracking the pointer for as long as it stays within the same word
 * (see moveTo()) — EditorViewport only asks for a fresh server request
 * when the pointer actually leaves that range. Purely informational —
 * WA_TransparentForMouseEvents (inherited), never focusable, never
 * intercepts a click.
 */
class HoverPanel : public TrackingPopup {
public:
    explicit HoverPanel(EditorViewport *viewport);

    /* Shows `text` (plain text — v1 doesn't render markdown, see
     * docs/adr/0030) in a word-wrapped box anchored near `pos`
     * (viewport-local pixel coordinates, typically just above the
     * mouse). Empty `text` is a no-op-safe way to say "nothing to
     * show" — callers should check before calling, but this won't
     * misbehave if they don't. */
    void showText(const QString &text, const QPoint &pos);
    /* Retargets to a new anchor point without changing the currently
     * shown text or re-measuring its size — cheap, called on every
     * qualifying mouse move while still within the hovered word's
     * range, so the tooltip visibly (and smoothly, via retarget's
     * glide) tracks the pointer instead of staying pinned to wherever
     * it first appeared. A no-op if nothing is currently shown. */
    void moveTo(const QPoint &pos);
    bool isShowingHover() const { return isTrackingVisible(); }

    void refreshTheme() override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *m_label;
    QSize m_lastContentSize;
};

#endif /* ASE_HOVER_PANEL_H */
