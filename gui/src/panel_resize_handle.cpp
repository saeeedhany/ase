#include "panel_resize_handle.h"

#include "motion.h"

#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace {
/* Tall enough to grab without aiming, short enough to read as a seam
 * rather than a gap. The line inside it is one pixel. */
constexpr int kGrabHeight = 7;
/* Present but not competing: visible enough to say the panel can be
 * resized, dim enough to ignore while reading. */
constexpr double kRestStrength = 0.22;
} // namespace

PanelResizeHandle::PanelResizeHandle(QWidget *parent) : QWidget(parent) {
    setFixedHeight(kGrabHeight);
    setCursor(Qt::SizeVerCursor);
    /* So the pointer arriving is enough; no button required. */
    setMouseTracking(true);
    m_strength = kRestStrength;

    m_fade = new QVariantAnimation(this);
    m_fade->setDuration(motion::kFade);
    m_fade->setEasingCurve(motion::kCurve);
    connect(m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_strength = value.toDouble();
        update();
    });
}

void PanelResizeHandle::setColor(const QColor &color) {
    m_color = color;
    update();
}

void PanelResizeHandle::animateTo(double strength) {
    m_fade->stop();
    m_fade->setStartValue(m_strength);
    m_fade->setEndValue(strength);
    m_fade->start();
}

void PanelResizeHandle::paintEvent(QPaintEvent *) {
    if (!m_color.isValid()) {
        return;
    }
    QPainter painter(this);
    QColor line = m_color;
    line.setAlphaF(m_color.alphaF() * m_strength);
    /* One pixel, centred in the grab area, edge to edge. A seam runs
     * the width of what it separates; a short centred bar reads as a
     * handle sitting on top of the layout rather than as the join. */
    double y = (height() - 1.0) / 2.0;
    painter.fillRect(QRectF(0.0, y, width(), 1.0), line);
}

void PanelResizeHandle::enterEvent(QEnterEvent *event) {
    animateTo(1.0);
    QWidget::enterEvent(event);
}

void PanelResizeHandle::leaveEvent(QEvent *event) {
    if (!m_dragging) {
        animateTo(kRestStrength);
    }
    QWidget::leaveEvent(event);
}

void PanelResizeHandle::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_lastY = event->globalPosition().toPoint().y();
    animateTo(1.0);
}

void PanelResizeHandle::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    /* Reported as a delta rather than an absolute position: the handle
     * does not know how tall the panel is, and the panel does not need
     * to know where the pointer is. */
    int y = event->globalPosition().toPoint().y();
    int delta = y - m_lastY;
    if (delta != 0) {
        m_lastY = y;
        emit dragged(delta);
    }
}

void PanelResizeHandle::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragging) {
        m_dragging = false;
        /* underMouse() and not rect().contains(): the pointer may have
         * left during the drag, which is normal when dragging fast. */
        animateTo(underMouse() ? 1.0 : kRestStrength);
    }
    QWidget::mouseReleaseEvent(event);
}
