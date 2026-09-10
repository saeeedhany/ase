#ifndef ASE_FLOATING_PANEL_H
#define ASE_FLOATING_PANEL_H

#include <QColor>
#include <QRect>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QParallelAnimationGroup;

/*
 * Base for every centered, floating chrome window in this app — find/
 * replace today, Open/Save-As and others later. See docs/adr/0022 for
 * the design this codifies: a plain child widget (not a top-level
 * QWindow) raised above its host and kept self-centered on the host's
 * full geometry (re-centering on host resize, via an event filter
 * installed on the host — no signal from the host is needed). Paints a
 * flat, translucent background plus a thin low-alpha border — no
 * shadows, no gradients, no rounded corners. Subclasses own their own
 * content/layout entirely; this class only owns show/hide/center/animate.
 *
 * Open/close animates opacity and a small scale-from-96% together (see
 * docs/adr/0022's animation-polish addendum) — fast (150ms) and
 * one-shot, not the continuous text-motion the `animations` config key
 * otherwise gates (caret fade, smooth scroll), but wired to the same
 * key anyway: one lever for "does this app move," not a second knob.
 */
class FloatingPanel : public QWidget {
public:
    explicit FloatingPanel(QWidget *host);

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

    QWidget *m_host;
    QColor m_panelBackground;
    QColor m_borderColor;
    bool m_animated = true;
    QGraphicsOpacityEffect *m_opacityEffect;
    QPropertyAnimation *m_opacityAnimation;
    QPropertyAnimation *m_geometryAnimation;
    QParallelAnimationGroup *m_animGroup;
};

#endif /* ASE_FLOATING_PANEL_H */
