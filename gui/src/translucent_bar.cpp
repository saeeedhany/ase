#include "translucent_bar.h"

#include <QPainter>

TranslucentBar::TranslucentBar(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void TranslucentBar::setColor(const QColor &color) {
    m_color = color;
    update();
}

void TranslucentBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), m_color);
}
