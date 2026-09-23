#include "buffer_bar.h"

#include "motion.h"

#include <algorithm>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QVariantAnimation>

namespace {
/* Opacity alone separates the active tab: no underline, box or
 * separator. Tiers match the gutter's. */
constexpr int kActiveAlpha = 255;
constexpr int kInactiveAlpha = 120;
constexpr int kHoverAlpha = 185;
/* Stays readable on an inactive tab: telling you about a file you are
 * not looking at is the whole reason it exists. */
constexpr int kDirtyDotMinAlpha = 200;

constexpr double kDotRadius = 3.0;
constexpr int kDotTextGap = 8;
constexpr int kNameCloseGap = 10;
constexpr int kEntryPadding = 14;
/* Generous, so the strip reads as part of the editor. */
constexpr int kBarVerticalPadding = 10;
constexpr double kCloseArm = 4.0;
constexpr int kCloseHitWidth = 20;
constexpr int kPlusArm = 5.0;
constexpr int kPlusHitWidth = 34;
} // namespace

namespace bufferbar {

/* Long enough for almost every real filename, short enough that one
 * pathological name cannot push the strip off the screen. */
int maxTabNameWidth(const QFontMetrics &metrics) {
    return metrics.averageCharWidth() * 22;
}

QString elideTabName(const QFontMetrics &metrics, const QString &name, int maxWidth) {
    if (maxWidth <= 0 || metrics.horizontalAdvance(name) <= maxWidth) {
        return name;
    }
    return metrics.elidedText(name, Qt::ElideMiddle, maxWidth);
}

} // namespace bufferbar

BufferBar::BufferBar(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true); /* hover state without a button held */
    setAttribute(Qt::WA_OpaquePaintEvent);

    /* Restarting mid-flight is intended: relayout() captures where
     * each tab is now, so an interrupted slide continues. */
    m_animation = new QVariantAnimation(this);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setDuration(motion::kChrome);
    m_animation->setEasingCurve(motion::kCurve);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_transition = value.toDouble();
        update();
    });
    connect(m_animation, &QVariantAnimation::finished, this, [this]() {
        bool removed = false;
        for (int i = m_tabs.size() - 1; i >= 0; --i) {
            if (m_tabs[i].closing) {
                m_tabs.remove(i);
                removed = true;
            }
        }
        if (removed) {
            update();
        }
    });
}

void BufferBar::setBaseFont(const QFont &font) {
    if (m_baseFont == font) {
        return;
    }
    m_baseFont = font;
    relayout(false, m_activeIndex);
    updateGeometry();
    update();
}

void BufferBar::setColors(const QColor &background, const QColor &text) {
    m_background = background;
    m_text = text;
    update();
}

int BufferBar::barHeight() const {
    return QFontMetrics(m_baseFont).height() + 2 * kBarVerticalPadding;
}

QSize BufferBar::sizeHint() const {
    /* Always present, even for one buffer: hiding it made the strip
     * appear and disappear as files opened and closed. */
    if (m_items.isEmpty()) {
        return QSize(0, 0);
    }
    return QSize(0, barHeight());
}

void BufferBar::setEntries(const QVector<Item> &items, int activeIndex) {
    bool same = items.size() == m_items.size() && activeIndex == m_activeIndex;
    for (int i = 0; same && i < items.size(); ++i) {
        same = items[i].id == m_items[i].id && items[i].name == m_items[i].name &&
               items[i].dirty == m_items[i].dirty;
    }
    if (same) {
        return;
    }

    /* Switching tabs lands instantly; animating it made every click
     * replay a slide, which read as stuttering. */
    QVector<quintptr> previousIds;
    for (const Item &item : m_items) {
        previousIds.push_back(item.id);
    }
    QVector<quintptr> nextIds;
    for (const Item &item : items) {
        nextIds.push_back(item.id);
    }
    bool setChanged = previousIds != nextIds;

    int previousActive = m_activeIndex;
    bool hadTabs = !m_tabs.isEmpty();
    m_items = items;
    m_activeIndex = activeIndex;
    m_hoverIndex = -1;
    m_hoverClose = false;
    relayout(hadTabs && setChanged, previousActive);
    updateGeometry(); /* a one-buffer strip hides itself — see sizeHint */
    update();
}

void BufferBar::relayout(bool animate, int previousActiveIndex) {
    /* Where each tab is drawn right now, so an interrupted animation
     * resumes from the visible position. */
    QVector<quintptr> previousIds;
    QVector<QString> previousNames;
    QVector<double> previousX;
    QVector<double> previousAlpha;
    QVector<double> previousWidth;
    previousIds.reserve(m_tabs.size());
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].closing) {
            continue; /* a ghost from a previous close; let it go */
        }
        previousIds.push_back(m_tabs[i].id);
        previousNames.push_back(m_tabs[i].name);
        previousX.push_back(m_tabs[i].fromX + (m_tabs[i].x - m_tabs[i].fromX) * m_transition);
        previousWidth.push_back(m_tabs[i].width);
        double settled = (i == previousActiveIndex) ? kActiveAlpha : kInactiveAlpha;
        previousAlpha.push_back(m_tabs[i].fromAlpha + (settled - m_tabs[i].fromAlpha) * m_transition);
    }

    QFontMetrics metrics(m_baseFont);
    m_tabs.clear();
    m_tabs.reserve(m_items.size());

    double x = 0.0;
    for (int i = 0; i < m_items.size(); ++i) {
        Tab tab;
        tab.id = m_items[i].id;
        /* Shortened here, so the width computed below is the one drawn. */
        tab.name = bufferbar::elideTabName(metrics, m_items[i].name,
                                            bufferbar::maxTabNameWidth(metrics));
        tab.dirty = m_items[i].dirty;
        /* Every tab reserves the close mark's width, so it fades in
         * rather than shoving neighbours sideways. */
        tab.width = kEntryPadding + 2 * kDotRadius + kDotTextGap + metrics.horizontalAdvance(tab.name) +
                     kNameCloseGap + 2 * kCloseArm + kEntryPadding;
        tab.x = x;
        x += tab.width;

        int wasAt = previousIds.indexOf(tab.id);
        if (wasAt >= 0) {
            tab.fromX = previousX[wasAt];
            /* Survivors keep their opacity; crossfading them made every
             * other tab visibly re-appear on a close. */
            tab.fromAlpha = (i == m_activeIndex) ? kActiveAlpha : kInactiveAlpha;
        } else {
            /* From the tab to its left, not the active one: tabs are
             * appended, so an active tab further left meant a long slide
             * across the whole strip. */
            if (previousX.isEmpty()) {
                tab.fromX = tab.x; /* the very first tab has nothing to emerge from */
            } else {
                int neighbour = std::min(i - 1, static_cast<int>(previousX.size()) - 1);
                tab.fromX = (neighbour >= 0) ? previousX[neighbour] : previousX.constFirst();
            }
            tab.fromAlpha = 0.0;
        }
        m_tabs.push_back(tab);
    }

    /* Collapses into whichever tab now holds focus. Stays drawn, and
     * un-clickable, until the animation lands. */
    if (animate) {
        for (int i = 0; i < previousIds.size(); ++i) {
            if (m_items.cend() != std::find_if(m_items.cbegin(), m_items.cend(),
                                                [&](const Item &item) { return item.id == previousIds[i]; })) {
                continue; /* still open */
            }
            Tab ghost;
            ghost.id = previousIds[i];
            ghost.name = previousNames.value(i);
            ghost.closing = true;
            ghost.width = previousWidth[i];
            ghost.fromX = previousX[i];
            ghost.fromAlpha = previousAlpha[i];
            int target = std::clamp(m_activeIndex, 0, static_cast<int>(m_tabs.size()) - 1);
            ghost.x = m_tabs.isEmpty() ? ghost.fromX : m_tabs[target].x;
            m_tabs.push_back(ghost);
        }
    }

    clampScroll();
    m_animation->stop();
    if (animate && !m_baseFont.family().isEmpty()) {
        m_transition = 0.0;
        m_animation->start();
    } else {
        m_transition = 1.0;
    }
}

double BufferBar::contentWidth() const {
    double widest = 0.0;
    for (const Tab &tab : m_tabs) {
        if (!tab.closing) {
            widest = std::max(widest, tab.x + tab.width);
        }
    }
    return widest;
}

void BufferBar::clampScroll() {
    /* The + has its own reserved strip, so tabs never slide under it. */
    double visible = std::max(0.0, static_cast<double>(width() - kPlusHitWidth));
    m_scrollX = std::clamp(m_scrollX, 0.0, std::max(0.0, contentWidth() - visible));
}

double BufferBar::tabLeft(int index) const {
    const Tab &tab = m_tabs[index];
    return tab.fromX + (tab.x - tab.fromX) * m_transition - m_scrollX;
}

/* Shift+wheel pans the strip; a plain wheel is left alone so it still
 * reaches the editor underneath. */
void BufferBar::wheelEvent(QWheelEvent *event) {
    double visible = std::max(0.0, static_cast<double>(width() - kPlusHitWidth));
    if (contentWidth() <= visible) {
        QWidget::wheelEvent(event);
        return;
    }
    QPoint pixels = event->pixelDelta();
    QPoint degrees = event->angleDelta();
    double delta = pixels.isNull() ? (degrees.y() != 0 ? degrees.y() : degrees.x()) / 2.0
                                   : (pixels.y() != 0 ? pixels.y() : pixels.x());
    m_scrollX -= delta;
    clampScroll();
    update();
    event->accept();
}

QRect BufferBar::closeRectFor(int index) const {
    const Tab &tab = m_tabs[index];
    int centerX = static_cast<int>(tabLeft(index) + tab.width - kEntryPadding - kCloseArm);
    return QRect(centerX - kCloseHitWidth / 2, 0, kCloseHitWidth, barHeight());
}

QRect BufferBar::plusRect() const {
    return QRect(width() - kPlusHitWidth, 0, kPlusHitWidth, barHeight());
}

int BufferBar::tabAt(const QPoint &pos) const {
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].closing) {
            continue; /* already gone as far as the user is concerned */
        }
        QRect bounds(static_cast<int>(tabLeft(i)), 0, static_cast<int>(m_tabs[i].width), barHeight());
        if (bounds.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void BufferBar::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    clampScroll(); /* a wider window can reveal tabs we were scrolled past */
    update();      /* the + lives against the right edge */
}

void BufferBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), m_background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(m_baseFont);

    QFontMetrics metrics(m_baseFont);
    double centerY = barHeight() / 2.0 + 0.5;

    for (int i = 0; i < m_tabs.size(); ++i) {
        const Tab &tab = m_tabs[i];
        double left = tabLeft(i);

        double settledAlpha = tab.closing ? 0.0
                               : (i == m_activeIndex ? kActiveAlpha
                                                     : (i == m_hoverIndex ? kHoverAlpha : kInactiveAlpha));
        int alpha = static_cast<int>(
            std::clamp(tab.fromAlpha + (settledAlpha - tab.fromAlpha) * m_transition, 0.0, 255.0));

        QColor color = m_text;
        color.setAlpha(alpha);

        double dotX = left + kEntryPadding + kDotRadius;
        if (tab.dirty) {
            QColor dotColor = m_text;
            dotColor.setAlpha(std::max(alpha, kDirtyDotMinAlpha));
            painter.setPen(Qt::NoPen);
            painter.setBrush(dotColor);
            painter.drawEllipse(QPointF(dotX, centerY), kDotRadius, kDotRadius);
        }

        int nameLeft = static_cast<int>(dotX + kDotRadius) + kDotTextGap;
        painter.setPen(color);
        painter.drawText(QRect(nameLeft, 0, metrics.horizontalAdvance(tab.name), barHeight()),
                          Qt::AlignLeft | Qt::AlignVCenter, tab.name);

        /* Shows only on the active tab, but its space is always
         * reserved — see relayout. */
        double closeOpacity = (!tab.closing && i == m_activeIndex) ? m_transition : 0.0;
        if (closeOpacity > 0.01) {
            QColor closeColor = m_text;
            int base = (m_hoverClose && m_hoverIndex == i) ? kActiveAlpha : kInactiveAlpha;
            closeColor.setAlpha(static_cast<int>(base * closeOpacity));
            QPen pen(closeColor);
            pen.setWidthF(1.4);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            double cx = left + tab.width - kEntryPadding - kCloseArm;
            painter.drawLine(QPointF(cx - kCloseArm, centerY - kCloseArm),
                              QPointF(cx + kCloseArm, centerY + kCloseArm));
            painter.drawLine(QPointF(cx + kCloseArm, centerY - kCloseArm),
                              QPointF(cx - kCloseArm, centerY + kCloseArm));
        }
    }

    /* Pinned right and painted last, over its own reserved strip. */
    painter.fillRect(plusRect(), m_background);
    QColor plusColor = m_text;
    plusColor.setAlpha(m_hoverPlus ? kActiveAlpha : kInactiveAlpha);
    QPen plusPen(plusColor);
    plusPen.setWidthF(1.4);
    plusPen.setCapStyle(Qt::RoundCap);
    painter.setPen(plusPen);
    double px = plusRect().center().x() + 0.5;
    painter.drawLine(QPointF(px - kPlusArm, centerY), QPointF(px + kPlusArm, centerY));
    painter.drawLine(QPointF(px, centerY - kPlusArm), QPointF(px, centerY + kPlusArm));
}

void BufferBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    QPoint pos = event->position().toPoint();
    if (plusRect().contains(pos)) {
        emit newBufferRequested();
        return;
    }
    int index = tabAt(pos);
    if (index < 0) {
        return;
    }
    if (index == m_activeIndex && closeRectFor(index).contains(pos)) {
        emit bufferCloseRequested(index);
        return;
    }
    emit bufferSelected(index);
}

void BufferBar::mouseMoveEvent(QMouseEvent *event) {
    QPoint pos = event->position().toPoint();
    bool overPlus = plusRect().contains(pos);
    int index = overPlus ? -1 : tabAt(pos);
    bool overClose = index >= 0 && index == m_activeIndex && closeRectFor(index).contains(pos);
    if (index != m_hoverIndex || overClose != m_hoverClose || overPlus != m_hoverPlus) {
        m_hoverIndex = index;
        m_hoverClose = overClose;
        m_hoverPlus = overPlus;
        update();
    }
}

void BufferBar::leaveEvent(QEvent *) {
    if (m_hoverIndex != -1 || m_hoverClose || m_hoverPlus) {
        m_hoverIndex = -1;
        m_hoverClose = false;
        m_hoverPlus = false;
        update();
    }
}
