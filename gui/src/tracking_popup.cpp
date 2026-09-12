#include "tracking_popup.h"

#include "editor_viewport.h"

#include <algorithm>

#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPropertyAnimation>

#include "motion.h"

TrackingPopup::TrackingPopup(EditorViewport *viewport) : QWidget(viewport), m_viewport(viewport) {
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(1.0);
    setGraphicsEffect(m_opacityEffect);

    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    motion::apply(m_fadeAnimation, motion::kFade);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacityEffect->opacity() <= 0.001) {
            hide();
        }
    });

    /* QWidget's own "pos" property (READ pos / WRITE move) — no custom
     * property needed, this is the standard way to animate a widget's
     * position in Qt. */
    m_moveAnimation = new QPropertyAnimation(this, "pos", this);
    /* The slower chrome tier, not kFade — a position glide reads better
     * with a little more travel time than a fade does. */
    motion::apply(m_moveAnimation, motion::kChrome);

    hide();
}

bool TrackingPopup::isTrackingVisible() const {
    return isVisible() && m_opacityEffect->opacity() > 0.001;
}

void TrackingPopup::refreshTheme() {
    if (m_viewport == nullptr) {
        return;
    }
    m_background = m_viewport->panelBackgroundColor();
    m_border = m_viewport->panelBorderColor();
    update();
}

void TrackingPopup::retarget(const QPoint &anchorPos, const QSize &size) {
    QRect hostRect = m_viewport->rect();
    int x = std::clamp(anchorPos.x(), 0, std::max(0, hostRect.width() - size.width()));
    int y = anchorPos.y();
    if (y + size.height() > hostRect.height()) {
        /* No room below the anchor — flip to just above it. */
        y = std::max(0, anchorPos.y() - size.height());
    }
    y = std::clamp(y, 0, std::max(0, hostRect.height() - size.height()));
    QPoint target(x, y);

    bool wasVisible = isTrackingVisible();
    resize(size);

    if (!wasVisible) {
        /* First appearance: snap straight to position (sliding in from
         * an arbitrary stale point would look wrong) and let the fade
         * carry the "arriving" feel instead. */
        m_moveAnimation->stop();
        move(target);
        raise();

        m_fadeAnimation->stop();
        if (!m_viewport->animationsEnabled()) {
            m_opacityEffect->setOpacity(1.0);
            show();
        } else {
            m_opacityEffect->setOpacity(0.0);
            show();
            m_fadeAnimation->setStartValue(0.0);
            m_fadeAnimation->setEndValue(1.0);
            m_fadeAnimation->start();
        }
        return;
    }

    raise();
    if (!m_viewport->animationsEnabled() || pos() == target) {
        m_moveAnimation->stop();
        move(target);
        return;
    }

    m_moveAnimation->stop();
    m_moveAnimation->setStartValue(pos());
    m_moveAnimation->setEndValue(target);
    m_moveAnimation->start();
}

void TrackingPopup::dismiss() {
    m_moveAnimation->stop();
    m_fadeAnimation->stop();
    if (!m_viewport->animationsEnabled()) {
        hide();
        return;
    }
    if (!isVisible()) {
        return;
    }
    m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->start();
}

void TrackingPopup::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), m_background);
    painter.setPen(m_border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
