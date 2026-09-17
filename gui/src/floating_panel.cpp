#include "floating_panel.h"

#include <algorithm>

#include <QCursor>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

#include "letter_badge.h"
#include "motion.h"

#include <QBoxLayout>
#include <QFont>

namespace {
constexpr int kHostMargin = 16; /* never touch the host's edges, even on a small window */

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
        motion::apply(anim, motion::kChrome);
    }

    /* Animates m_snapshot only, never the real geometry, so each frame
     * is a scaled pixmap blit rather than a live relayout. */
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
    if (m_dragging) {
        return; /* don't fight a drag in progress — see setDragHandle's doc comment */
    }
    setGeometry(targetGeometry());
}

void FloatingPanel::addTitleBar(QVBoxLayout *layout, QChar badge, const QString &title,
                                 LetterBadge **badgeOut, QLabel **titleOut) {
    auto *bar = new QWidget(contentWidget());
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    *badgeOut = new LetterBadge(badge, bar);
    (*badgeOut)->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(*badgeOut);

    *titleOut = new QLabel(title, bar);
    (*titleOut)->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont titleFont = (*titleOut)->font();
    titleFont.setBold(true);
    (*titleOut)->setFont(titleFont);
    row->addWidget(*titleOut);
    row->addStretch(1);

    layout->addWidget(bar);
    setDragHandle(bar);
}

void FloatingPanel::setDragHandle(QWidget *handle) {
    if (m_dragHandle != nullptr) {
        m_dragHandle->removeEventFilter(this);
    }
    m_dragHandle = handle;
    if (m_dragHandle != nullptr) {
        m_dragHandle->installEventFilter(this);
    }
}

/* Synchronously: plain setGeometry() only schedules the layout for the
 * next event-loop pass. */
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

/* Re-syncs geometry *after* the show: that first show can trigger Qt's
 * own auto-sizing on a freshly-visible child, overriding correct
 * geometry that read fine right up until then. Idempotent. */
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
    QRect start = shrunkAround(full, motion::kPopScale);
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
    m_geometryAnimation->setEndValue(shrunkAround(full, motion::kPopScale));
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

    if (watched == m_dragHandle) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragStartMouse = mouseEvent->globalPosition().toPoint();
                m_dragStartPanelPos = pos();
                /* Consumed, not just handled: a plain QWidget drag
                 * handle (docs/adr/0044 made the whole header bar one,
                 * not just the badge) has no mousePressEvent override
                 * of its own, so Qt's default "ignored event
                 * propagates to the parent" rule would otherwise walk
                 * this press straight up through content -> this panel
                 * -> m_host (EditorViewport), which started
                 * misinterpreting it as a click on the document the
                 * moment EditorViewport gained its own "click outside
                 * a modal panel closes it" logic. Returning true here
                 * stops that walk at the source. Was a latent bug even
                 * before that — a badge press without any drag motion
                 * would have silently moved the document cursor
                 * underneath the panel the same way. */
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            QPoint delta = mouseEvent->globalPosition().toPoint() - m_dragStartMouse;
            QPoint newPos = m_dragStartPanelPos + delta;
            if (m_host != nullptr) {
                int maxX = std::max(0, m_host->width() - width());
                int maxY = std::max(0, m_host->height() - height());
                newPos.setX(std::clamp(newPos.x(), 0, maxX));
                newPos.setY(std::clamp(newPos.y(), 0, maxY));
            }
            move(newPos);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_dragging) {
            m_dragging = false;
            restoreFocusAfterDrag();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}
