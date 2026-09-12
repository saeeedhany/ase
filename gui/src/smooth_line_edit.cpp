#include "smooth_line_edit.h"

#include "motion.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QProxyStyle>
#include <QTimer>

namespace {

/* The caret breathe cycle and the hard-blink half-period, both in ticks
 * of motion::kTickMs — the same numbers EditorViewport uses (24 ticks
 * ~= 720ms, toggling every 12). Duplicated as a deliberate two-line
 * copy rather than promoted to motion.h: they belong to "what a caret
 * does", and the only other caret in the app is the editor's own, whose
 * copy lives in its own renderer. If a third caret ever appears, that is
 * the moment to move them. */
constexpr int kCaretAnimationTicks = 24;
constexpr int kBlinkToggleTicks = 12;
constexpr double kTwoPi = 6.283185307179586;

/* Matches kCaretWidth in the editor's own internal header — the panels
 * cannot include that (it is private to editor_viewport*.cpp, see
 * docs/adr/0052), and a caret that is 2px in the editor and 1px in a
 * panel would defeat the point of this whole widget. */
constexpr double kCaretWidth = 2.0;

/* Below this, the glide has arrived: stop asymptotically crawling and
 * land, exactly as the editor's own eased values do. */
constexpr double kSnapDistance = 0.5;

/*
 * Reports a zero-width text cursor and defers everything else to the
 * real style. QLineEdit reads PM_TextCursorWidth when it draws, so this
 * removes the native caret without touching any other painting.
 *
 * One instance shared by every SmoothLineEdit and never deleted:
 * QWidget::setStyle does not take ownership, the object must outlive
 * every widget using it, and it holds no per-widget state.
 */
class NoNativeCaretStyle : public QProxyStyle {
public:
    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override {
        if (metric == PM_TextCursorWidth) {
            return 0;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

QStyle *noNativeCaretStyle() {
    static auto *style = new NoNativeCaretStyle();
    return style;
}

} // namespace

SmoothLineEdit::SmoothLineEdit(QWidget *parent) : QLineEdit(parent) {
    setStyle(noNativeCaretStyle());

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &SmoothLineEdit::tick);

    /* Any of these means the caret just moved or the text under it
     * changed, so the breathe/blink phase restarts at full brightness —
     * the "never fade out while you are actively using it" rule the
     * editor's resetCaretBlink() enforces. */
    auto wake = [this]() {
        m_idleTicks = 0;
        m_caretVisible = true;
        update();
    };
    connect(this, &QLineEdit::cursorPositionChanged, this, wake);
    connect(this, &QLineEdit::textChanged, this, wake);
    connect(this, &QLineEdit::selectionChanged, this, wake);
}

void SmoothLineEdit::setAnimated(bool animated) {
    if (m_animated == animated) {
        return;
    }
    m_animated = animated;
    m_caretVisible = true;
    m_idleTicks = 0;
    update();
}

/*
 * The heartbeat runs only while the field has focus — a panel that is
 * closed, or open with focus in its list rather than its field, costs
 * nothing. Without this, five fields across five panels per buffer would
 * each wake the process 33 times a second forever.
 */
void SmoothLineEdit::tick() {
    m_idleTicks++;

    if (m_animated) {
        double target = caretTargetX();
        double delta = target - m_caretX;
        if (m_caretX < 0.0 || std::abs(delta) < kSnapDistance) {
            m_caretX = target;
        } else {
            m_caretX += delta * motion::kEaseFactor;
        }
        update();
        return;
    }

    m_caretX = caretTargetX();
    if (m_idleTicks % kBlinkToggleTicks == 0) {
        m_caretVisible = !m_caretVisible;
        update();
    }
}

double SmoothLineEdit::caretTargetX() const {
    QRect rect = cursorRect();
    /* cursorRect() pads its width on both sides so a repaint of it
     * covers the antialiased glyph edges either side of the caret; the
     * caret itself is that rect's own cursor-width slice, which is zero
     * here. See the header for why this is derived rather than a
     * literal. */
    return rect.x() + std::ceil(rect.width() / 2.0);
}

void SmoothLineEdit::paintEvent(QPaintEvent *event) {
    QLineEdit::paintEvent(event);

    if (!hasFocus()) {
        return;
    }

    if (m_caretX < 0.0) {
        m_caretX = caretTargetX();
    }

    /* cos, not sin, so phase 0 — the instant the caret goes idle — is
     * full brightness: the fade always reads as "was solid, now easing
     * into the cycle" rather than jumping into the middle of it
     * (docs/adr/0017). */
    int alpha = 255;
    if (m_animated) {
        double phase = (m_idleTicks % kCaretAnimationTicks) / static_cast<double>(kCaretAnimationTicks);
        alpha = std::clamp(static_cast<int>(128 + 127 * std::cos(phase * kTwoPi)), 0, 255);
    } else if (!m_caretVisible) {
        return;
    }

    QColor color = palette().color(QPalette::Text);
    color.setAlpha(alpha);

    QRect rect = cursorRect();
    QPainter painter(this);
    painter.fillRect(QRectF(m_caretX, rect.y(), kCaretWidth, rect.height()), color);
}

void SmoothLineEdit::focusInEvent(QFocusEvent *event) {
    QLineEdit::focusInEvent(event);
    /* Arrive where the caret actually is. A panel opens with its field
     * pre-filled and selected often enough that gliding in from the last
     * caret position — or from the left edge — would read as a glitch
     * rather than as motion. */
    m_caretX = caretTargetX();
    m_idleTicks = 0;
    m_caretVisible = true;
    m_timer->start(motion::kTickMs);
    update();
}

void SmoothLineEdit::focusOutEvent(QFocusEvent *event) {
    QLineEdit::focusOutEvent(event);
    m_timer->stop();
    update();
}

void SmoothLineEdit::showEvent(QShowEvent *event) {
    QLineEdit::showEvent(event);
    /* Geometry is only final once shown, so a target computed before
     * this point can be stale by a whole panel width. */
    m_caretX = -1.0;
}
