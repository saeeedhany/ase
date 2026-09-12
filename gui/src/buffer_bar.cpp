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
/* Opacity is the only thing separating the active tab from the rest —
 * no underline, no box, no separator (see the class comment). Tiers
 * match the ones the gutter already uses for "this line is yours /
 * these are context". */
constexpr int kActiveAlpha = 255;
constexpr int kInactiveAlpha = 120;
constexpr int kHoverAlpha = 185;
/* The unsaved-changes dot stays readable even on an inactive tab —
 * telling you about a file you are *not* looking at is the entire
 * reason it exists, so it must not fade out with the name. */
constexpr int kDirtyDotMinAlpha = 200;

constexpr double kDotRadius = 3.0;
constexpr int kDotTextGap = 8;
constexpr int kNameCloseGap = 10;
constexpr int kEntryPadding = 14;
/* Generous next to the old 7px — the strip reads as part of the editor
 * rather than a thin afterthought stuck above it. */
constexpr int kBarVerticalPadding = 10;
constexpr double kCloseArm = 4.0;
constexpr int kCloseHitWidth = 20;
constexpr int kPlusArm = 5.0;
constexpr int kPlusHitWidth = 34;
} // namespace

BufferBar::BufferBar(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true); /* hover state without a button held */
    setAttribute(Qt::WA_OpaquePaintEvent);

    /* One animation drives every layout change. Restarting it mid-flight
     * is fine and intended: relayout() captures wherever each tab is
     * *right now* as its new starting point, so interrupting a slide
     * continues from the visible position rather than snapping. */
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
    /* Always present, even for a single buffer. It was hidden below two
     * tabs originally (the name is already in the title bar), but that
     * makes the strip appear and disappear under you as you open and
     * close files, and leaves a pathless buffer with nothing anywhere
     * calling it "untitled". A tab is where you look for what you have
     * open; it should always be there. See docs/adr/0057. */
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

    /* Only opening and closing move anything; switching active tab is a
     * plain state change and should land instantly. Animating it too
     * meant every single click replayed a slide/fade, which reads as the
     * app stuttering rather than responding. See docs/adr/0057. */
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
    /* Where each tab is being drawn *at this instant*, so an interrupted
     * animation resumes from the visible position instead of jumping
     * back to wherever the last one started. */
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
        tab.name = m_items[i].name;
        tab.dirty = m_items[i].dirty;
        /* Every tab reserves the close mark's width whether or not it is
         * the active one. That is the whole fix for the strip jumping:
         * the mark fades in on the active tab instead of pushing its
         * neighbours sideways. */
        tab.width = kEntryPadding + 2 * kDotRadius + kDotTextGap + metrics.horizontalAdvance(tab.name) +
                     kNameCloseGap + 2 * kCloseArm + kEntryPadding;
        tab.x = x;
        x += tab.width;

        int wasAt = previousIds.indexOf(tab.id);
        if (wasAt >= 0) {
            tab.fromX = previousX[wasAt];
            /* Survivors keep the opacity they already have — only their
             * *position* may move. Crossfading them too meant closing one
             * tab made every other tab visibly re-appear. */
            tab.fromAlpha = (i == m_activeIndex) ? kActiveAlpha : kInactiveAlpha;
        } else {
            /* A new tab emerges from the tab immediately to its left —
             * the one it is about to sit beside — not from whichever tab
             * happened to be active. New tabs are always appended, so an
             * active tab further left meant a long slide across the whole
             * strip that read as coming from the wrong place. Starting
             * from the neighbour keeps the distance one tab wide and the
             * gesture identical no matter how many are already open. */
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

    /* A closed tab collapses back into whichever tab now holds focus —
     * the exact reverse of arriving out of the tab it was opened from.
     * It stays drawn, and un-clickable, until the animation lands. */
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
    /* The + keeps its own reserved strip at the right edge, so tabs are
     * never allowed to slide underneath it. */
    double visible = std::max(0.0, static_cast<double>(width() - kPlusHitWidth));
    m_scrollX = std::clamp(m_scrollX, 0.0, std::max(0.0, contentWidth() - visible));
}

double BufferBar::tabLeft(int index) const {
    const Tab &tab = m_tabs[index];
    return tab.fromX + (tab.x - tab.fromX) * m_transition - m_scrollX;
}

/* Shift+wheel pans the strip once there are more tabs than fit —
 * matching the convention every browser and terminal already uses for
 * horizontal scroll, rather than inventing a key. A plain wheel is left
 * alone so it keeps reaching the editor underneath. See docs/adr/0057. */
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

        /* The close mark only *shows* on the active tab, but its space is
         * always reserved (see relayout) — so it fades rather than
         * shoving the strip around. */
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

    /* New-tab affordance, pinned to the right edge so it doesn't move as
     * tabs come and go. Painted last, over its own reserved strip, so a
     * panned tab never slides across it. */
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
