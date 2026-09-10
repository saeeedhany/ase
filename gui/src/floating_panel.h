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
 * Base for every centered, floating chrome window in this app — find/
 * replace and Open/Save-As today, others later. See docs/adr/0022 for
 * the design this codifies, and its animation-approach addendum
 * (docs/adr/0024) for why open/close animates a *snapshot image*
 * rather than the real widget's geometry.
 *
 * A plain child widget (not a top-level QWindow) raised above its
 * host, kept self-centered on the host's full geometry (re-centering
 * on host resize, via an event filter installed on the host — no
 * signal from the host is needed). Paints a flat, translucent
 * background plus a thin low-alpha border — no shadows, no gradients,
 * no rounded corners.
 *
 * Subclasses must build their content on contentWidget(), not on
 * `this` directly — FloatingPanel needs to be able to hide that
 * content and show a static snapshot in its place during animation,
 * so animating a scale+fade never triggers real child-layout
 * recalculation (the cause of a real, reported jank when this instead
 * animated the widget's own `geometry`, forcing QVBoxLayout/QListWidget
 * to relayout on every frame).
 */
class FloatingPanel : public QWidget {
public:
    /* Where targetGeometry() places the panel over its host. Center is
     * the default and what every panel used before user feedback asked
     * find/replace specifically to move — see docs/adr/0026: a
     * centered find bar sits on top of the text you're searching,
     * which reads worse for a panel you keep open while scanning
     * matches than for a glance-act-dismiss one like Open/Save-As. */
    enum class Anchor { Center, TopRight };

    explicit FloatingPanel(QWidget *host);

    /* Subclasses lay out their real UI on this widget, not on `this`. */
    QWidget *contentWidget() const { return m_content; }

    /* Set once, typically right after construction — not re-checked
     * per open the way colors/animation are, since a panel's screen
     * position isn't config-driven. */
    void setAnchor(Anchor anchor) { m_anchor = anchor; }

    /* Makes contentWidget() visible and correctly, finally sized — call
     * this, populate any data-dependent child view (e.g. a
     * QListWidget's rows), *then* call openPanel(). openPanel() calls
     * this too, so calling it yourself first is optional, not required
     * — but if a child view needs to compute its own internal row/
     * scroll geometry from real content, that population must happen
     * between this call and openPanel()'s snapshot grab, not before
     * it. Populating such a view before the panel had ever been shown
     * produced real, reproducible bugs (top rows landing permanently
     * "scrolled out of view", wrong highlight-rect geometry): a plain
     * setGeometry() while hidden doesn't survive the content's *first*
     * show — Qt's own first-show auto-sizing for a freshly-visible
     * child (observed with QListWidget specifically) can override it
     * again, after the fact, even though geometry() reads correctly
     * right up until that first show() call. This method's job is to
     * absorb that and leave geometry genuinely settled. See
     * docs/adr/0024. */
    void revealForSetup();

    /* Both colors are pushed in by the subclass (from EditorViewport's
     * config-driven accessors) rather than read here — FloatingPanel
     * has no config access of its own, so it stays reusable for any
     * future panel without core/config coupling. */
    void setColors(const QColor &background, const QColor &border);
    /* Per-open toggle, not a constructor flag — the owner re-checks the
     * live `animations` config value each time a panel opens/closes, so
     * a hot-reloaded config change takes effect immediately. */
    void setAnimated(bool enabled) { m_animated = enabled; }

    /* Centers over the host (host size queried fresh, so a resize since
     * the last open is picked up), raises above it, and animates in (or
     * snaps visible if not animated). */
    void openPanel();
    /* Animates out, then hides (or snaps hidden if not animated). */
    void closePanel();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /* The host-centered, full-size rect this panel should occupy right
     * now — recomputed on every call, never cached, so it's always
     * correct even if the host was resized since the last open. */
    QRect targetGeometry() const;
    /* Non-animated snap to targetGeometry() — used for live re-centering
     * while already open (a host resize isn't a user open/close action,
     * so it shouldn't replay the pop-in). */
    void recenter();
    /* Forces contentWidget()'s geometry (and its layout's child
     * geometry) to match this panel's rect synchronously — see
     * docs/adr/0024. */
    void syncContentGeometry();
    /* Grabs contentWidget()'s current appearance (with this panel's own
     * flat background/border baked in — see paintEvent) into a static
     * pixmap on m_snapshot, then hides the real content and shows that
     * pixmap in its place — see the class comment. Assumes
     * revealForSetup() already ran (openPanel() ensures this). */
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
};

#endif /* ASE_FLOATING_PANEL_H */
