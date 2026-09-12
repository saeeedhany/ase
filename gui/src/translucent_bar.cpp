#include "translucent_bar.h"

#include <QPainter>

TranslucentBar::TranslucentBar(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void TranslucentBar::setColor(const QColor &color) {
    m_color = color;
    update();
}

void TranslucentBar::setRadius(double radius) {
    m_radius = radius;
    update();
}

void TranslucentBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    if (m_radius > 0.0) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        painter.drawRoundedRect(QRectF(rect()), m_radius, m_radius);
        return;
    }
    painter.fillRect(rect(), m_color);
}
