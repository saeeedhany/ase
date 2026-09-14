#ifndef ASE_FLOATING_PANEL_H
#define ASE_FLOATING_PANEL_H

#include <QColor>
#include <QRect>
#include <QWidget>

class QLabel;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QParallelAnimationGroup;

/*
 * Base for every centred floating chrome window. A plain child widget
 * raised above its host and re-centred on host resize via an event
 * filter. See docs/adr/0022.
 *
 * Subclasses must build on contentWidget(), not on `this`: open/close
 * animates a snapshot image in place of the real content, because
 * animating the widget's own geometry relayouts every child on every
 * frame and visibly janks. See docs/adr/0024.
 */
class FloatingPanel : public QWidget {
public:
    /* Centre is the default; find/replace moved because a centred bar
     * sits on the text you are searching. See docs/adr/0026. */
    enum class Anchor { Center, TopRight };

    explicit FloatingPanel(QWidget *host);

    /* Subclasses lay out their real UI on this widget, not on `this`. */
    QWidget *contentWidget() const { return m_content; }

    /* Set once; not config-driven, so not re-checked per open. */
    void setAnchor(Anchor anchor) { m_anchor = anchor; }

    /* Makes contentWidget() visible and finally sized. A child view
     * that computes geometry from its content must be populated
     * between this call and openPanel()'s snapshot grab: setGeometry()
     * while hidden does not survive Qt's first-show auto-sizing, which
     * left QListWidget rows scrolled out of view. See docs/adr/0024. */
    void revealForSetup();

    /* Pushed in by the subclass: this class has no config access. */
    void setColors(const QColor &background, const QColor &border);
    /* Per-open, so a hot-reloaded config change takes effect at once. */
    void setAnimated(bool enabled) { m_animated = enabled; }

    /* Centres over the host, raises, and animates in. */
    void openPanel();
    /* Animates out, then hides. */
    void closePanel();

    /* Lets `handle` drag the panel, clamped inside the host. A drag is
     * per-open, not persisted: the next open recentres. */
    void setDragHandle(QWidget *handle);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    /* Subclasses reclaim keyboard focus here: releasing a drag left it
     * on the host, so Escape silently did nothing afterwards. */
    virtual void restoreFocusAfterDrag() {}

private:
    /* Recomputed every call, never cached. */
    QRect targetGeometry() const;
    /* Snap, not animate: a host resize shouldn't replay the pop-in. */
    void recenter();
    /* Synchronously matches contentWidget() to this panel's rect. */
    void syncContentGeometry();
    /* Grabs contentWidget() into m_snapshot and shows that in its
     * place. Assumes revealForSetup() already ran. */
    void beginSnapshotAnimation();

    QWidget *m_host;
    QWidget *m_content;
    QLabel *m_snapshot;
    bool m_showingSnapshot = false;

    QColor m_panelBackground;
    QColor m_borderColor;
    bool m_animated = true;
    Anchor m_anchor = Anchor::Center;
    QGraphicsOpacityEffect *m_opacityEffect;
    QPropertyAnimation *m_opacityAnimation;
    QPropertyAnimation *m_geometryAnimation;
    QParallelAnimationGroup *m_animGroup;

    QWidget *m_dragHandle = nullptr;
    bool m_dragging = false;
    QPoint m_dragStartMouse;   /* global pos at press */
    QPoint m_dragStartPanelPos; /* this->pos() at press */
};

#endif /* ASE_FLOATING_PANEL_H */
