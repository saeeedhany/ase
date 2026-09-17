#ifndef ASE_PANEL_RESIZE_HANDLE_H
#define ASE_PANEL_RESIZE_HANDLE_H

#include <QWidget>

class QVariantAnimation;

/*
 * The seam between the editor and a docked panel, and the thing you
 * drag to resize it.
 *
 * A hairline rather than a bar: it marks where one region ends and the
 * next begins, which needs a line, not an object. It sits dim until the
 * pointer is near it, because a divider is not something to look at
 * while reading code — the same reasoning as the dimmed gutter in
 * ADR 0014.
 *
 * The line is one pixel; the widget is several, so it can be grabbed
 * without aiming. See docs/adr/0117.
 */
class PanelResizeHandle : public QWidget {
    Q_OBJECT

public:
    explicit PanelResizeHandle(QWidget *parent);

    /* The line's colour at full strength; it is drawn at a fraction of
     * this until hovered. */
    void setColor(const QColor &color);

signals:
    /* Pointer moved `delta` pixels while held. Positive is downward,
     * which makes a panel below the seam shorter. */
    void dragged(int delta);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void animateTo(double strength);

    QColor m_color;
    /* 0..1, animated. Not a Qt property on purpose: QPropertyAnimation
     * needs one, so it lives on a QVariantAnimation instead and this
     * stays a plain double the paint reads. */
    double m_strength = 0.0;
    bool m_dragging = false;
    int m_lastY = 0;
    QVariantAnimation *m_fade = nullptr;
};

#endif /* ASE_PANEL_RESIZE_HANDLE_H */
