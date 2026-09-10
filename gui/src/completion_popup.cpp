#include "completion_popup.h"

#include "editor_viewport.h"

#include <algorithm>

#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPropertyAnimation>

namespace {
constexpr int kRowPaddingX = 8;
constexpr int kMaxVisibleRows = 8;
constexpr int kMinWidth = 160;
constexpr int kMaxWidth = 420;
constexpr int kDetailGap = 16;
constexpr int kFadeDurationMs = 90; /* faster than FloatingPanel's 110ms — see the header's doc comment */
} // namespace

CompletionPopup::CompletionPopup(EditorViewport *viewport) : QWidget(viewport), m_viewport(viewport) {
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(1.0);
    setGraphicsEffect(m_opacityEffect);

    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnimation->setDuration(kFadeDurationMs);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacityEffect->opacity() <= 0.001) {
            hide();
        }
    });

    refreshTheme();
    hide();
}

void CompletionPopup::refreshTheme() {
    if (m_viewport == nullptr) {
        return;
    }
    m_background = m_viewport->panelBackgroundColor();
    m_border = m_viewport->panelBorderColor();
    m_textColor = m_viewport->textColor();
    m_dimColor = m_textColor;
    m_dimColor.setAlpha(140);
    m_selectionColor = m_viewport->selectionColor();
    update();
}

QSize CompletionPopup::contentSize() const {
    QFontMetrics metrics(font());
    int rowHeight = metrics.height() + 6;
    int width = kMinWidth;
    for (const Item &item : m_items) {
        int w = metrics.horizontalAdvance(item.label) + 2 * kRowPaddingX;
        if (!item.detail.isEmpty()) {
            w += kDetailGap + metrics.horizontalAdvance(item.detail);
        }
        width = std::max(width, w);
    }
    width = std::min(width, kMaxWidth);
    int rows = std::min(static_cast<int>(m_items.size()), kMaxVisibleRows);
    return QSize(width, rows * rowHeight + 2);
}

void CompletionPopup::ensureSelectionVisible() {
    if (m_selected < m_scrollOffset) {
        m_scrollOffset = m_selected;
    } else if (m_selected >= m_scrollOffset + kMaxVisibleRows) {
        m_scrollOffset = m_selected - kMaxVisibleRows + 1;
    }
}

void CompletionPopup::showItems(const QVector<Item> &items, const QPoint &pos) {
    m_items = items;
    m_selected = 0;
    m_scrollOffset = 0;

    if (m_items.isEmpty()) {
        dismiss();
        return;
    }

    bool wasVisible = isVisible() && m_opacityEffect->opacity() > 0.001;

    QSize size = contentSize();
    QRect hostRect = m_viewport->rect();
    int x = std::clamp(pos.x(), 0, std::max(0, hostRect.width() - size.width()));
    int y = pos.y();
    if (y + size.height() > hostRect.height()) {
        /* No room below the caret line — flip to just above it. `pos`
         * is already the caret's *bottom* edge, so subtracting the
         * popup height plus one line's worth clears the line itself. */
        y = std::max(0, pos.y() - size.height());
    }

    m_fadeAnimation->stop();
    setGeometry(x, y, size.width(), size.height());
    raise();

    if (!m_viewport->animationsEnabled()) {
        m_opacityEffect->setOpacity(1.0);
        show();
        update();
        return;
    }

    if (!wasVisible) {
        m_opacityEffect->setOpacity(0.0);
        show();
        m_fadeAnimation->setStartValue(0.0);
        m_fadeAnimation->setEndValue(1.0);
        m_fadeAnimation->start();
    } else {
        /* Already open — just refresh in place, no re-fade. */
        show();
        update();
    }
}

void CompletionPopup::dismiss() {
    m_items.clear();
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

void CompletionPopup::moveSelection(int delta) {
    if (m_items.isEmpty()) {
        return;
    }
    int count = static_cast<int>(m_items.size());
    m_selected = (m_selected + delta) % count;
    if (m_selected < 0) {
        m_selected += count;
    }
    ensureSelectionVisible();
    update();
}

const CompletionPopup::Item *CompletionPopup::selectedItem() const {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size())) {
        return nullptr;
    }
    return &m_items[m_selected];
}

void CompletionPopup::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), m_background);
    painter.setPen(m_border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    QFontMetrics metrics(font());
    int rowHeight = metrics.height() + 6;
    painter.setFont(font());

    int visibleRows = std::min(static_cast<int>(m_items.size()) - m_scrollOffset, kMaxVisibleRows);
    for (int row = 0; row < visibleRows; ++row) {
        int index = m_scrollOffset + row;
        int y = 1 + row * rowHeight;
        const Item &item = m_items[index];

        if (index == m_selected) {
            painter.fillRect(QRect(1, y, width() - 2, rowHeight), m_selectionColor);
        }

        painter.setPen(m_textColor);
        painter.drawText(QRect(kRowPaddingX, y, width() - 2 * kRowPaddingX, rowHeight),
                          Qt::AlignLeft | Qt::AlignVCenter, item.label);

        if (!item.detail.isEmpty()) {
            painter.setPen(m_dimColor);
            painter.drawText(QRect(kRowPaddingX, y, width() - 2 * kRowPaddingX, rowHeight),
                              Qt::AlignRight | Qt::AlignVCenter, item.detail);
        }
    }
}
