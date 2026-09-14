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
 * Shared base for the caret-tracking overlays (CompletionPopup,
 * HoverPanel). Not FloatingPanels: those centre on the host and stay
 * put, these follow a moving point and refresh constantly. See
 * docs/adr/0031.
 *
 * Gives them: the app's flat translucent panel paint; a fade on the
 * hidden->visible edge only, so a content change never replays it; and
 * a position glide toward each retarget(), while size applies
 * immediately. All snap when animations are off.
 */
class TrackingPopup : public QWidget {
public:
    explicit TrackingPopup(EditorViewport *viewport);

    void dismiss();
    bool isTrackingVisible() const;

    /* Subclasses override to refresh their own colours too, calling
     * this first. */
    virtual void refreshTheme();

protected:
    /* Places a `size` box at `pos`, clamped inside the viewport and
     * flipping above when there is no room below. Safe to call on
     * every content or anchor change, including an unchanged one. */
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
