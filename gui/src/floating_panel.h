#ifndef ASE_FLOATING_PANEL_H
#define ASE_FLOATING_PANEL_H

#include <QColor>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;

/*
 * Base for every centered, floating chrome window in this app — find/
 * replace today, Open/Save-As and others later. See docs/adr/0022 for
 * the design this codifies: a plain child widget (not a top-level
 * QWindow) raised above its host and kept self-centered on the host's
 * full geometry (re-centering on host resize, via an event filter
 * installed on the host — no signal from the host is needed). Paints a
 * flat, translucent background plus a thin low-alpha border — no
 * shadows, no gradients, no rounded corners. Subclasses own their own
 * content/layout entirely; this class only owns show/hide/center/fade.
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

    /* Centers over the host, raises above it, and fades in (or snaps
     * visible if not animated). */
    void openPanel();
    /* Fades out, then hides (or snaps hidden if not animated). */
    void closePanel();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void recenter();

    QWidget *m_host;
    QColor m_panelBackground;
    QColor m_borderColor;
    bool m_animated = true;
    QGraphicsOpacityEffect *m_opacityEffect;
    QPropertyAnimation *m_fadeAnimation;
};

#endif /* ASE_FLOATING_PANEL_H */
