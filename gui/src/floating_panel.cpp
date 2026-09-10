#include "floating_panel.h"

#include <algorithm>

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPropertyAnimation>

namespace {
constexpr int kFadeDurationMs = 140; /* fast and clean — see docs/adr/0022 */
constexpr int kHostMargin = 16;      /* never touch the host's edges, even on a small window */
}

FloatingPanel::FloatingPanel(QWidget *host) : QWidget(host), m_host(host) {
    setAutoFillBackground(false);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnimation->setDuration(kFadeDurationMs);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    /* One persistent connection rather than reconnecting per open/close
     * — only actually hides when the fade that just finished was a
     * fade-*out* (ended at opacity 0); a finished fade-in leaves the
     * panel visible. */
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacityEffect->opacity() <= 0.001) {
            hide();
        }
    });

    hide();
    if (m_host != nullptr) {
        m_host->installEventFilter(this);
    }
}

void FloatingPanel::setColors(const QColor &background, const QColor &border) {
    m_panelBackground = background;
    m_borderColor = border;
    update();
}

void FloatingPanel::recenter() {
    if (m_host == nullptr) {
        return;
    }
    QRect hostRect = m_host->rect();
    QSize size = sizeHint();
    size.setWidth(std::min(size.width(), std::max(1, hostRect.width() - 2 * kHostMargin)));
    size.setHeight(std::min(size.height(), std::max(1, hostRect.height() - 2 * kHostMargin)));
    int x = hostRect.x() + (hostRect.width() - size.width()) / 2;
    int y = hostRect.y() + (hostRect.height() - size.height()) / 2;
    setGeometry(x, y, size.width(), size.height());
}

void FloatingPanel::openPanel() {
    recenter();
    raise();
    show();
    m_fadeAnimation->stop();
    if (m_animated) {
        m_opacityEffect->setOpacity(0.0);
        m_fadeAnimation->setStartValue(0.0);
        m_fadeAnimation->setEndValue(1.0);
        m_fadeAnimation->start();
    } else {
        m_opacityEffect->setOpacity(1.0);
    }
}

void FloatingPanel::closePanel() {
    m_fadeAnimation->stop();
    if (m_animated) {
        m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
        m_fadeAnimation->setEndValue(0.0);
        m_fadeAnimation->start();
    } else {
        m_opacityEffect->setOpacity(0.0);
        hide();
    }
}

void FloatingPanel::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), m_panelBackground);
    painter.setPen(m_borderColor);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

bool FloatingPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_host && event->type() == QEvent::Resize && isVisible()) {
        recenter();
    }
    return QWidget::eventFilter(watched, event);
}
