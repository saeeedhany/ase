#ifndef ASE_TRACKING_POPUP_H
#define ASE_TRACKING_POPUP_H

#include <QColor>
#include <QPoint>
#include <QSize>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class EditorViewport;

/*
 * Shared base for the app's small, transient, cursor/caret-tracking
 * overlays (CompletionPopup, HoverPanel) — see docs/adr/0031. Neither
 * is a FloatingPanel (docs/adr/0022, docs/adr/0024): those are host-
 * centered, glance-act-dismiss chrome windows that scale+fade open
 * once and stay put. These track a moving point and refresh far more
 * often, so this base gives them a different, shared identity instead:
 *
 *  - the same flat, translucent panel_background/border paint every
 *    other piece of chrome in this app uses (this class's paintEvent;
 *    a subclass overrides paintEvent to call TrackingPopup::paintEvent
 *    first, then draws its own content on top in a second QPainter)
 *  - a fast fade, but only on the hidden->visible edge — a still-open
 *    popup whose content just changed (a new keystroke's completion
 *    list, a fresher hover response) never replays it
 *  - once visible, a smooth *position* glide toward wherever
 *    retarget() is next called with, instead of jumping — this is what
 *    makes a hover tooltip track the pointer within the same word, and
 *    a completion popup slide along with the caret as you keep typing.
 *    Size still applies immediately (no glide), only position eases.
 *  - both skip straight to a snap when animationsEnabled() is false,
 *    same as everywhere else in the app.
 *
 * A future tracking overlay derives from this and gets all of the
 * above for free: implement your own content/sizing, call retarget()
 * with the desired anchor and size, done — nothing to reimplement.
 */
class TrackingPopup : public QWidget {
public:
    explicit TrackingPopup(EditorViewport *viewport);

    void dismiss();
    bool isTrackingVisible() const;

    /* Re-pulls background/border from the viewport's theme. Subclasses
     * override to also refresh their own content's colors, calling
     * this base implementation first. */
    virtual void refreshTheme();

protected:
    /* Moves (snapping on first appearance, or with animations off,
     * else gliding) to place a `size`-sized box with its top-left at
     * `pos` — clamped to stay fully inside the viewport, flipping
     * above `pos` instead of below when there isn't room. Resizes
     * instantly. Fades in only when transitioning from hidden to
     * visible. Call whenever a subclass's content or target anchor
     * changes — cheap and safe to call every time, including with an
     * unchanged position (a no-op move in that case). */
    void retarget(const QPoint &pos, const QSize &size);

    void paintEvent(QPaintEvent *event) override;

    EditorViewport *m_viewport;
    QColor m_background;
    QColor m_border;

private:
    QGraphicsOpacityEffect *m_opacityEffect;
    QPropertyAnimation *m_fadeAnimation;
    QPropertyAnimation *m_moveAnimation;
};

#endif /* ASE_TRACKING_POPUP_H */
