#include "hover_panel.h"

#include "editor_viewport.h"

#include <algorithm>

#include <QFontMetrics>
#include <QLabel>
#include <QPainter>

namespace {
constexpr int kPadding = 8;
constexpr int kMaxWidth = 480;
} // namespace

HoverPanel::HoverPanel(EditorViewport *viewport) : TrackingPopup(viewport) {
    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);

    refreshTheme();
}

void HoverPanel::refreshTheme() {
    TrackingPopup::refreshTheme();
    if (m_viewport == nullptr) {
        return;
    }
    QColor text = m_viewport->textColor();
    QPalette pal = m_label->palette();
    pal.setColor(QPalette::WindowText, text);
    m_label->setPalette(pal);
}

void HoverPanel::showText(const QString &text, const QPoint &pos) {
    if (text.isEmpty()) {
        dismiss();
        return;
    }

    /* Measured directly via QFontMetrics::boundingRect rather than
     * trusting QLabel's own word-wrap layout pass to have settled by
     * the time we read a size back from it — this project already hit
     * a whole class of "geometry wrong until some later Qt-internal
     * layout pass" bugs doing that with QListWidget (docs/adr/0024);
     * measuring explicitly sidesteps it entirely, in the same spirit
     * as xForColumn measuring real glyph widths instead of assuming
     * them (docs/adr/0013). */
    QFontMetrics metrics(m_label->font());
    int innerWidth = std::min(kMaxWidth, metrics.boundingRect(QRect(0, 0, kMaxWidth - 2 * kPadding, 0),
                                                                Qt::TextWordWrap, text)
                                              .width());
    innerWidth = std::max(innerWidth, 80 - 2 * kPadding);
    int textHeight = metrics.boundingRect(QRect(0, 0, innerWidth, 0), Qt::TextWordWrap, text).height();

    m_label->setText(text);
    m_label->setGeometry(kPadding, kPadding, innerWidth, textHeight);
    m_lastContentSize = QSize(innerWidth + 2 * kPadding, textHeight + 2 * kPadding);

    retarget(pos, m_lastContentSize);
}

void HoverPanel::moveTo(const QPoint &pos) {
    if (!isTrackingVisible()) {
        return;
    }
    retarget(pos, m_lastContentSize);
}

void HoverPanel::paintEvent(QPaintEvent *event) {
    TrackingPopup::paintEvent(event);
}
