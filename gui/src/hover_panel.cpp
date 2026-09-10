#include "hover_panel.h"

#include "editor_viewport.h"

#include <algorithm>

#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>

namespace {
constexpr int kPadding = 8;
constexpr int kMaxWidth = 480;
constexpr int kFadeDurationMs = 90;
} // namespace

HoverPanel::HoverPanel(EditorViewport *viewport) : QWidget(viewport), m_viewport(viewport) {
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);

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

void HoverPanel::refreshTheme() {
    if (m_viewport == nullptr) {
        return;
    }
    m_background = m_viewport->panelBackgroundColor();
    m_border = m_viewport->panelBorderColor();
    QColor text = m_viewport->textColor();
    QPalette pal = m_label->palette();
    pal.setColor(QPalette::WindowText, text);
    m_label->setPalette(pal);
    update();
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
    QSize size(innerWidth + 2 * kPadding, textHeight + 2 * kPadding);

    QRect hostRect = m_viewport->rect();
    int x = std::clamp(pos.x(), 0, std::max(0, hostRect.width() - size.width()));
    int y = pos.y() - size.height();
    if (y < 0) {
        y = pos.y(); /* not enough room above — show below the pointer instead */
    }
    y = std::clamp(y, 0, std::max(0, hostRect.height() - size.height()));

    bool wasVisible = isVisible() && m_opacityEffect->opacity() > 0.001;

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
        show();
        update();
    }
}

void HoverPanel::dismiss() {
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

void HoverPanel::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), m_background);
    painter.setPen(m_border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
