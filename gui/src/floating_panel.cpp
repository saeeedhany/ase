#include "floating_panel.h"

#include <algorithm>

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

namespace {
constexpr int kAnimDurationMs = 110; /* fast and clean — see docs/adr/0022; was 150, sped up per docs/adr/0027 */
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

    m_content = new QWidget(this);
    m_snapshot = new QLabel(this);
    m_snapshot->setScaledContents(true);
    m_snapshot->hide();

    m_opacityEffect = new QGraphicsOpacityEffect(m_snapshot);
    m_opacityEffect->setOpacity(1.0);
    m_snapshot->setGraphicsEffect(m_opacityEffect);

    m_opacityAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_geometryAnimation = new QPropertyAnimation(m_snapshot, "geometry", this);
    m_animGroup = new QParallelAnimationGroup(this);
    m_animGroup->addAnimation(m_opacityAnimation);
    m_animGroup->addAnimation(m_geometryAnimation);
    for (QPropertyAnimation *anim : {m_opacityAnimation, m_geometryAnimation}) {
        anim->setDuration(kAnimDurationMs);
        anim->setEasingCurve(QEasingCurve::OutCubic);
    }

    /* Only ever animating m_snapshot (a plain image-filled QLabel with
     * no children of its own), never this panel's real `geometry` or
     * its content's layout — so every frame is just a scaled pixmap
     * blit, not a live QVBoxLayout/QListWidget relayout. That relayout
     * cost was a real, reported jank in the previous design. See
     * docs/adr/0024. */
    connect(m_animGroup, &QParallelAnimationGroup::finished, this, [this]() {
        if (m_opacityEffect->opacity() >= 0.999) {
            /* Finished opening: swap back to the real, interactive
             * content now that it's fully visible. */
            m_showingSnapshot = false;
            m_snapshot->hide();
            m_content->show();
            update();
        } else if (m_opacityEffect->opacity() <= 0.001) {
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
    QSize size = m_content->sizeHint();
    size.setWidth(std::min(size.width(), std::max(1, hostRect.width() - 2 * kHostMargin)));
    size.setHeight(std::min(size.height(), std::max(1, hostRect.height() - 2 * kHostMargin)));

    int x;
    int y;
    if (m_anchor == Anchor::TopRight) {
        x = hostRect.x() + hostRect.width() - size.width() - kHostMargin;
        y = hostRect.y() + kHostMargin;
    } else {
        x = hostRect.x() + (hostRect.width() - size.width()) / 2;
        y = hostRect.y() + (hostRect.height() - size.height()) / 2;
    }
    return QRect(x, y, size.width(), size.height());
}

void FloatingPanel::recenter() {
    setGeometry(targetGeometry());
}

/* Forces contentWidget()'s geometry (and its layout's child geometry)
 * to match `this` panel's own rect *right now*, synchronously — plain
 * setGeometry()/layout invalidation alone only schedules that for the
 * next event-loop pass. See docs/adr/0024. */
void FloatingPanel::syncContentGeometry() {
    m_content->setGeometry(rect());
    if (m_content->layout() != nullptr) {
        m_content->layout()->activate();
    }
}

void FloatingPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    syncContentGeometry();
    m_snapshot->setGeometry(rect());
}

/* Resizes to target, raises, shows both this panel and contentWidget(),
 * and re-syncs content geometry *after* that show — contentWidget()'s
 * show() here is its subtree's first real show, which can trigger Qt's
 * own first-show auto-sizing on a freshly-visible child (observed with
 * QListWidget specifically), overriding whatever correct geometry was
 * already assigned before it, even though geometry() read correctly
 * right up until that call. Re-syncing immediately after is what
 * actually makes it stick — see docs/adr/0024. Safe to call more than
 * once (e.g. openPanel() also calls it): every step here is idempotent
 * once geometry has actually settled. */
void FloatingPanel::revealForSetup() {
    setGeometry(targetGeometry());
    raise();
    show();
    m_content->show();
    syncContentGeometry();
}

/* Grabs contentWidget()'s current appearance (with this panel's own
 * flat background/border baked in — see paintEvent) into m_snapshot,
 * then hides the real content so only the static image represents the
 * panel while it animates. Assumes revealForSetup() already ran. */
void FloatingPanel::beginSnapshotAnimation() {
    m_showingSnapshot = false; /* so this panel's own paintEvent still draws the flat bg/border for the grab */
    QPixmap pixmap = grab();
    m_showingSnapshot = true;
    m_content->hide();

    m_snapshot->setPixmap(pixmap);
    m_snapshot->show();
    m_snapshot->raise();
}

void FloatingPanel::openPanel() {
    QRect target = targetGeometry();
    m_animGroup->stop();
    revealForSetup();

    if (!m_animated) {
        m_showingSnapshot = false;
        m_snapshot->hide();
        update();
        return;
    }

    beginSnapshotAnimation();

    QRect full(QPoint(0, 0), target.size());
    QRect start = shrunkAround(full, kPopScale);
    m_snapshot->setGeometry(start);
    m_opacityEffect->setOpacity(0.0);

    m_geometryAnimation->setStartValue(start);
    m_geometryAnimation->setEndValue(full);
    m_opacityAnimation->setStartValue(0.0);
    m_opacityAnimation->setEndValue(1.0);
    m_animGroup->start();
}

void FloatingPanel::closePanel() {
    m_animGroup->stop();

    if (!m_animated) {
        hide();
        m_showingSnapshot = false;
        return;
    }

    beginSnapshotAnimation();
    QRect full = rect();

    m_geometryAnimation->setStartValue(full);
    m_geometryAnimation->setEndValue(shrunkAround(full, kPopScale));
    m_opacityAnimation->setStartValue(1.0);
    m_opacityAnimation->setEndValue(0.0);
    m_animGroup->start();
}

void FloatingPanel::paintEvent(QPaintEvent *) {
    if (m_showingSnapshot) {
        return; /* m_snapshot (a child, always painted after us) represents the whole panel right now */
    }
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
