#include "buffer_bar.h"

#include <algorithm>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace {
/* Opacity is the *only* thing distinguishing the active buffer from the
 * rest — no underline, no box, no separator (see the class comment).
 * Tiers match the ones the gutter already uses for the same job of
 * "this line is yours / these are context": full strength for the
 * active one, the established dim tier for the others, and a small lift
 * on hover so an inactive entry still acknowledges the pointer without
 * introducing a hover plate. */
constexpr int kActiveAlpha = 255;
constexpr int kInactiveAlpha = 120;
constexpr int kHoverAlpha = 185;
/* The unsaved-changes dot stays readable even on an inactive entry —
 * telling you about a file you are *not* looking at is the entire
 * reason it exists, so it must not fade out with the name. */
constexpr int kDirtyDotMinAlpha = 200;

constexpr double kDotRadius = 2.5;
constexpr int kDotTextGap = 7;   /* dot to filename */
constexpr int kNameCloseGap = 9; /* filename to close mark */
constexpr int kEntryPadding = 11; /* each side of an entry */
constexpr int kBarVerticalPadding = 7;
constexpr double kCloseArm = 3.5; /* half-diagonal of the close x */
} // namespace

BufferBar::BufferBar(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true); /* hover state without a button held */
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void BufferBar::setEntries(const QVector<Item> &items, int activeIndex) {
    bool same = items.size() == m_items.size() && activeIndex == m_activeIndex;
    for (int i = 0; same && i < items.size(); ++i) {
        same = items[i].name == m_items[i].name && items[i].dirty == m_items[i].dirty;
    }
    if (same) {
        return;
    }
    m_items = items;
    m_activeIndex = activeIndex;
    m_hoverIndex = -1;
    m_hoverClose = false;
    layoutEntries();
    updateGeometry(); /* a one-buffer bar hides itself — see sizeHint */
    update();
}

void BufferBar::setColors(const QColor &background, const QColor &text) {
    m_background = background;
    m_text = text;
    update();
}

QSize BufferBar::sizeHint() const {
    /* One buffer needs no bar at all: the filename is already in the
     * window title, and showing a single entry would be chrome that
     * tells you nothing. Collapsing to zero height means someone who
     * never opens a second file sees exactly the editor they had before
     * this feature existed. */
    if (m_items.size() < 2) {
        return QSize(0, 0);
    }
    return QSize(0, QFontMetrics(font()).height() + 2 * kBarVerticalPadding);
}

void BufferBar::layoutEntries() {
    m_entries.clear();
    m_entries.reserve(m_items.size());

    QFontMetrics metrics(font());
    int height = metrics.height() + 2 * kBarVerticalPadding;
    int x = 0;

    for (int i = 0; i < m_items.size(); ++i) {
        int nameWidth = metrics.horizontalAdvance(m_items[i].name);
        /* The dot's slot is reserved whether or not it is drawn, so
         * names never shift sideways the moment a file becomes dirty —
         * a bar that reflows on your first keystroke is worse than a
         * little extra left padding. */
        int width = kEntryPadding + static_cast<int>(2 * kDotRadius) + kDotTextGap + nameWidth;
        if (i == m_activeIndex) {
            width += kNameCloseGap + static_cast<int>(2 * kCloseArm);
        }
        width += kEntryPadding;

        Entry entry;
        entry.name = m_items[i].name;
        entry.dirty = m_items[i].dirty;
        entry.bounds = QRect(x, 0, width, height);
        if (i == m_activeIndex) {
            int closeCenterX = x + width - kEntryPadding - static_cast<int>(kCloseArm);
            /* A generous square around the little x — a 7px glyph is a
             * miserable click target, and the padding around it is dead
             * space anyway. */
            entry.close = QRect(closeCenterX - 9, 0, 18, height);
        }
        m_entries.push_back(entry);
        x += width;
    }
}

int BufferBar::entryAt(const QPoint &pos) const {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].bounds.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void BufferBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), m_background);
    if (m_entries.size() != m_items.size()) {
        layoutEntries(); /* font changed under us since the last setEntries */
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    QFontMetrics metrics(font());

    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &entry = m_entries[i];

        int alpha = kInactiveAlpha;
        if (i == m_activeIndex) {
            alpha = kActiveAlpha;
        } else if (i == m_hoverIndex) {
            alpha = kHoverAlpha;
        }
        QColor color = m_text;
        color.setAlpha(alpha);

        double centerY = entry.bounds.center().y() + 0.5;
        double dotX = entry.bounds.left() + kEntryPadding + kDotRadius;
        if (entry.dirty) {
            QColor dotColor = m_text;
            dotColor.setAlpha(std::max(alpha, kDirtyDotMinAlpha));
            painter.setPen(Qt::NoPen);
            painter.setBrush(dotColor);
            painter.drawEllipse(QPointF(dotX, centerY), kDotRadius, kDotRadius);
        }

        int nameLeft = static_cast<int>(dotX + kDotRadius) + kDotTextGap;
        painter.setPen(color);
        painter.drawText(QRect(nameLeft, entry.bounds.top(), metrics.horizontalAdvance(entry.name),
                                entry.bounds.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, entry.name);

        /* Close mark only on the active entry — an x on every buffer
         * would be five things asking to be clicked instead of one. */
        if (i == m_activeIndex && !entry.close.isNull()) {
            QColor closeColor = m_text;
            closeColor.setAlpha(m_hoverClose && m_hoverIndex == i ? kActiveAlpha : kInactiveAlpha);
            QPen pen(closeColor);
            pen.setWidthF(1.3);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            double cx = entry.close.center().x() + 0.5;
            painter.drawLine(QPointF(cx - kCloseArm, centerY - kCloseArm),
                              QPointF(cx + kCloseArm, centerY + kCloseArm));
            painter.drawLine(QPointF(cx + kCloseArm, centerY - kCloseArm),
                              QPointF(cx - kCloseArm, centerY + kCloseArm));
        }
    }
}

void BufferBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    int index = entryAt(event->position().toPoint());
    if (index < 0) {
        return;
    }
    if (index == m_activeIndex && !m_entries[index].close.isNull() &&
        m_entries[index].close.contains(event->position().toPoint())) {
        emit bufferCloseRequested(index);
        return;
    }
    emit bufferSelected(index);
}

void BufferBar::mouseMoveEvent(QMouseEvent *event) {
    QPoint pos = event->position().toPoint();
    int index = entryAt(pos);
    bool overClose = index >= 0 && index == m_activeIndex && !m_entries[index].close.isNull() &&
                      m_entries[index].close.contains(pos);
    if (index != m_hoverIndex || overClose != m_hoverClose) {
        m_hoverIndex = index;
        m_hoverClose = overClose;
        update();
    }
}

void BufferBar::leaveEvent(QEvent *) {
    if (m_hoverIndex != -1 || m_hoverClose) {
        m_hoverIndex = -1;
        m_hoverClose = false;
        update();
    }
}
