#include "floating_panel.h"

#include <algorithm>

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

namespace {
constexpr int kAnimDurationMs = 150; /* fast and clean — see docs/adr/0022 */
constexpr int kHostMargin = 16;      /* never touch the host's edges, even on a small window */
constexpr double kPopScale = 0.96;   /* opens/closes scaling from/to this fraction of full size */

QRect shrunkAround(const QRect &target, double factor) {
    QSize size = target.size() * factor;
    QPoint topLeft(target.center().x() - size.width() / 2, target.center().y() - size.height() / 2);
    return QRect(topLeft, size);
}
} // namespace

FloatingPanel::FloatingPanel(QWidget *host) : QWidget(host), m_host(host) {
    setAutoFillBackground(false);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_opacityAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_geometryAnimation = new QPropertyAnimation(this, "geometry", this);
    m_animGroup = new QParallelAnimationGroup(this);
    m_animGroup->addAnimation(m_opacityAnimation);
    m_animGroup->addAnimation(m_geometryAnimation);
    for (QPropertyAnimation *anim : {m_opacityAnimation, m_geometryAnimation}) {
        anim->setDuration(kAnimDurationMs);
        anim->setEasingCurve(QEasingCurve::OutCubic);
    }

    /* One persistent connection rather than reconnecting per open/close
     * — only actually hides when the animation that just finished was
     * closing (ended at opacity 0); a finished open leaves it visible. */
    connect(m_animGroup, &QParallelAnimationGroup::finished, this, [this]() {
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

QRect FloatingPanel::targetGeometry() const {
    if (m_host == nullptr) {
        return QRect();
    }
    QRect hostRect = m_host->rect();
    QSize size = sizeHint();
    size.setWidth(std::min(size.width(), std::max(1, hostRect.width() - 2 * kHostMargin)));
    size.setHeight(std::min(size.height(), std::max(1, hostRect.height() - 2 * kHostMargin)));
    int x = hostRect.x() + (hostRect.width() - size.width()) / 2;
    int y = hostRect.y() + (hostRect.height() - size.height()) / 2;
    return QRect(x, y, size.width(), size.height());
}

void FloatingPanel::recenter() {
    setGeometry(targetGeometry());
}

void FloatingPanel::openPanel() {
    QRect target = targetGeometry();
    m_animGroup->stop();
    raise();
    show();

    if (m_animated) {
        QRect start = shrunkAround(target, kPopScale);
        setGeometry(start);
        m_opacityEffect->setOpacity(0.0);
        m_geometryAnimation->setStartValue(start);
        m_geometryAnimation->setEndValue(target);
        m_opacityAnimation->setStartValue(0.0);
        m_opacityAnimation->setEndValue(1.0);
        m_animGroup->start();
    } else {
        setGeometry(target);
        m_opacityEffect->setOpacity(1.0);
    }
}

void FloatingPanel::closePanel() {
    QRect target = targetGeometry();
    m_animGroup->stop();
    if (m_animated) {
        m_geometryAnimation->setStartValue(geometry());
        m_geometryAnimation->setEndValue(shrunkAround(target, kPopScale));
        m_opacityAnimation->setStartValue(m_opacityEffect->opacity());
        m_opacityAnimation->setEndValue(0.0);
        m_animGroup->start();
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
