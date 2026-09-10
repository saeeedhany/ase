#include "completion_popup.h"

#include "editor_viewport.h"

#include <algorithm>

#include <QFontMetrics>
#include <QPainter>

namespace {
constexpr int kRowPaddingX = 8;
constexpr int kMaxVisibleRows = 8;
constexpr int kMinWidth = 160;
constexpr int kMaxWidth = 420;
constexpr int kDetailGap = 16;
} // namespace

CompletionPopup::CompletionPopup(EditorViewport *viewport) : TrackingPopup(viewport) {
    refreshTheme();
}

void CompletionPopup::refreshTheme() {
    TrackingPopup::refreshTheme();
    if (m_viewport == nullptr) {
        return;
    }
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

    retarget(pos, contentSize());
    update();
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

void CompletionPopup::paintEvent(QPaintEvent *event) {
    TrackingPopup::paintEvent(event);

    QPainter painter(this);
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
