#include "editor_viewport.h"

#include "about_panel.h"
#include "command_line.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "help_panel.h"
#include "output_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <QClipboard>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>

namespace {
/* ~720ms period at the 30ms tick below — was 34 (~1020ms); sped up
 * along with every other animation in the app, see docs/adr/0027. */
constexpr int kCaretAnimationTicks = 24;
constexpr double kTwoPi = 6.283185307179586;
constexpr int kCaretWidth = 2;
constexpr int kGutterPadding = 8; /* on each side of the line-number text */
/* Per paint — see docs/adr/0015. Was 0.35; raised (converges faster
 * per frame) for a snappier glide, see docs/adr/0027. */
constexpr double kEaseFactor = 0.5;
/* "scrolloff"-style context margin, in lines/characters, kept visible
 * around the cursor before the view scrolls — see docs/adr/0024. */
constexpr int kVerticalScrollMargin = 3;
constexpr int kHorizontalScrollMarginChars = 4;

bool isUtf8ContinuationByte(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

bool isWordChar(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9') || uc == '_';
}

void collectHighlightSpan(void *user_data, AseHighlightSpan span) {
    static_cast<QVector<AseHighlightSpan> *>(user_data)->push_back(span);
}
} // namespace

EditorViewport::EditorViewport(AseBuffer *buffer, QString filePath, QWidget *parent)
    : QWidget(parent), m_buffer(buffer), m_filePath(std::move(filePath)) {
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::NoContextMenu);
    setAutoFillBackground(false);
    setAccessibleName(QStringLiteral("Editor"));
    setAccessibleDescription(QStringLiteral("Text editing area"));

    loadConfig();

    QString suffix = QFileInfo(m_filePath).suffix().toLower();
    if (suffix == QLatin1String("c") || suffix == QLatin1String("h")) {
        m_syntax = ase_syntax_create_c();
    }

    refreshCache();

    m_undo = ase_undo_create();

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_idleTicks++;
        if (m_animationsEnabled) {
            update(); /* the fade's phase is also idle-tick-driven — see docs/adr/0017 */
        } else if (m_idleTicks % 12 == 0) { /* ~360ms at this 30ms tick — was 17 (~500ms), see docs/adr/0016, docs/adr/0027 */
            m_caretVisible = !m_caretVisible;
            update();
        }
    });
    m_blinkTimer->start(30);

    m_configTimer = new QTimer(this);
    connect(m_configTimer, &QTimer::timeout, this, [this]() { checkConfigReload(); });
    m_configTimer->start(750); /* see docs/adr/0008, decision 4 */

    m_compilePollTimer = new QTimer(this);
    connect(m_compilePollTimer, &QTimer::timeout, this, [this]() { pollCompile(); });
}

EditorViewport::~EditorViewport() {
    ase_process_destroy(m_compileProcess);
    ase_syntax_destroy(m_syntax);
    ase_config_destroy(m_config);
    ase_undo_destroy(m_undo);
    ase_buffer_destroy(m_buffer);
}

void EditorViewport::loadConfig() {
    char *path = ase_config_default_path();
    if (path != nullptr) {
        m_configPath = QString::fromLocal8Bit(path);
        free(path);
        ase_config_write_default_if_missing(m_configPath.toUtf8().constData());
    }

    m_config = ase_config_load(m_configPath.isEmpty() ? nullptr : m_configPath.toUtf8().constData());
    applyConfig();

    if (!m_configPath.isEmpty()) {
        m_configModified = QFileInfo(m_configPath).lastModified();
    }
}

void EditorViewport::applyConfig() {
    uint8_t r, g, b, a;
    if (ase_config_get_color(m_config, "background", &r, &g, &b, &a)) {
        m_backgroundColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "text", &r, &g, &b, &a)) {
        m_textColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "selection", &r, &g, &b, &a)) {
        m_selectionColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "find_match", &r, &g, &b, &a)) {
        m_findMatchColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "panel_background", &r, &g, &b, &a)) {
        m_panelBackgroundColor = QColor(r, g, b, a);
    }

    const char *familyStr = ase_config_get_string(m_config, "font_family");
    QString family = familyStr != nullptr ? QString::fromUtf8(familyStr) : QStringLiteral("monospace");
    long size = ase_config_get_int(m_config, "font_size", 12);

    m_font = family.compare(QLatin1String("monospace"), Qt::CaseInsensitive) == 0
                 ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                 : QFont(family);
    m_font.setPointSize(static_cast<int>(size));

    /* Cached once here rather than reconstructed per run per paint — see
     * docs/adr/0017. */
    m_metrics = QFontMetrics(m_font);
    QFont boldFont = m_font;
    boldFont.setBold(true);
    m_boldMetrics = QFontMetrics(boldFont);
    QFont italicFont = m_font;
    italicFont.setItalic(true);
    m_italicMetrics = QFontMetrics(italicFont);

    m_lineHeight = m_metrics.height();
    m_charWidth = m_metrics.horizontalAdvance(QLatin1Char('M'));

    /* Opt-in, off by default — see docs/adr/0012, decision 2. */
    const char *animationsStr = ase_config_get_string(m_config, "animations");
    m_animationsEnabled = animationsStr != nullptr &&
                          QString::fromUtf8(animationsStr).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;

    /* On by default, unlike animations — see docs/adr/0014, decision 4.
     * An unrecognized value falls back to "absolute" rather than
     * treating it as an error — a bad config value should never break
     * the editor. */
    const char *lineNumbersStr = ase_config_get_string(m_config, "line_numbers");
    m_lineNumberMode = lineNumbersStr != nullptr ? QString::fromUtf8(lineNumbersStr).toLower()
                                                  : QStringLiteral("absolute");
    if (m_lineNumberMode != QLatin1String("off") && m_lineNumberMode != QLatin1String("relative")) {
        m_lineNumberMode = QStringLiteral("absolute");
    }
}

void EditorViewport::checkConfigReload() {
    if (m_configPath.isEmpty()) {
        return;
    }

    QDateTime modified = QFileInfo(m_configPath).lastModified();
    if (!modified.isValid() || modified == m_configModified) {
        return;
    }
    m_configModified = modified;

    ase_config_destroy(m_config);
    m_config = ase_config_load(m_configPath.toUtf8().constData());
    applyConfig();
    if (m_findBar != nullptr) {
        m_findBar->refreshTheme();
    }
    if (m_fileBrowser != nullptr) {
        m_fileBrowser->refreshTheme();
    }
    if (m_commandLine != nullptr) {
        m_commandLine->refreshTheme();
    }
    if (m_outputPanel != nullptr) {
        m_outputPanel->refreshTheme();
    }
    if (m_helpPanel != nullptr) {
        m_helpPanel->refreshTheme();
    }
    if (m_aboutPanel != nullptr) {
        m_aboutPanel->refreshTheme();
    }
    ensureCursorVisible();
    update();
}

void EditorViewport::refreshCache() {
    size_t len = ase_buffer_length(m_buffer);
    m_cache.resize(static_cast<qsizetype>(len));
    if (len > 0) {
        ase_buffer_get_text(m_buffer, 0, len, m_cache.data());
    }

    m_lineStarts.clear();
    m_lineStarts.push_back(0);
    for (int i = 0; i < m_cache.size(); ++i) {
        if (m_cache[i] == '\n') {
            m_lineStarts.push_back(i + 1);
        }
    }

    m_highlights.clear();
    if (m_syntax != nullptr) {
        ase_syntax_highlight(m_syntax, m_cache.constData(), static_cast<size_t>(m_cache.size()),
                              collectHighlightSpan, &m_highlights);
    }

    recomputeMatches();
}

int EditorViewport::lineForOffset(size_t offset) const {
    auto it = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), static_cast<int>(offset));
    return static_cast<int>(std::distance(m_lineStarts.begin(), it)) - 1;
}

int EditorViewport::columnForOffset(size_t offset, int line) const {
    return static_cast<int>(offset) - m_lineStarts[line];
}

size_t EditorViewport::offsetForLineColumn(int line, int column) const {
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int start = m_lineStarts[line];
    int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    column = std::clamp(column, 0, end - start);
    return static_cast<size_t>(start + column);
}

size_t EditorViewport::offsetForPoint(const QPoint &pos) const {
    /* Uses the *rendered* (possibly still-easing) scroll position, not
     * the logical target — a click during an active scroll animation
     * has to map against what's actually on screen right now. See
     * docs/adr/0015. The vertical fractional part is ignored (floor
     * only) — a sub-line-height miss mid-animation is a transient,
     * approximate case, consistent with docs/adr/0013's click-precision
     * philosophy. */
    int line = static_cast<int>(std::floor(m_renderedScrollLine)) + pos.y() / std::max(1, m_lineHeight);
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    /* Approximate on purpose: unlike the caret (docs/adr/0013), a click a
     * character off on a long or styled line is a minor miss, not a
     * growing visible bug — not worth an O(line length) exact-measurement
     * search on every click. Shifted by the gutter and horizontal scroll
     * (docs/adr/0014) to land in the same local coordinate space
     * paintEvent's translate uses. */
    int localX = std::max(0, static_cast<int>(pos.x() - gutterWidth() + m_renderedScrollX));
    /* Rounds to the *nearest* column instead of flooring to whichever
     * one the click's left edge falls in — a real, reported bug: a
     * plain floor meant a click in a character's right half still
     * resolved to that character, so the cursor only advanced once the
     * pointer had moved almost a full character further right than
     * expected. See docs/adr/0028. */
    int col = (localX + std::max(1, m_charWidth) / 2) / std::max(1, m_charWidth);
    size_t offset = offsetForLineColumn(line, col);

    /* Column counting is byte-based (see docs/adr/0012, decision 1) — a
     * pixel click can land mid-codepoint; snap forward to the next
     * lead-byte boundary so every cursor position stays one that the
     * multi-cursor edit operations' invariant assumes. */
    while (offset > 0 && offset < static_cast<size_t>(m_cache.size()) &&
           isUtf8ContinuationByte(m_cache[static_cast<int>(offset)])) {
        offset++;
    }
    return offset;
}

void EditorViewport::paintEvent(QPaintEvent *) {
    updateAnimation();

    QPainter painter(this);
    painter.fillRect(rect(), m_backgroundColor);

    int gutter = gutterWidth();
    int textAreaWidth = std::max(1, width() - gutter);

    int firstLine = std::max(0, static_cast<int>(std::floor(m_renderedScrollLine)));
    double fracLine = m_renderedScrollLine - std::floor(m_renderedScrollLine);
    /* +2, not +1: the fractional vertical shift below can reveal a line
     * beyond what a plain height()/lineHeight count would cover. */
    int visibleLines = std::max(1, height() / m_lineHeight + 2);
    int lastLine = std::min(firstLine + visibleLines, static_cast<int>(m_lineStarts.size()));

    /* Everything from here to restore() draws in "local" (document)
     * coordinates — the translate folds in the gutter offset, horizontal
     * scroll, and the sub-line-height vertical remainder together, so
     * drawLine/xForColumn didn't need to change at all. The clip keeps
     * any of it from painting over the gutter. See docs/adr/0014
     * (gutter/scroll) and docs/adr/0015 (the fractional part, added for
     * smooth scrolling). */
    painter.save();
    painter.translate(gutter - m_renderedScrollX, -fracLine * m_lineHeight);
    painter.setClipRect(QRectF(m_renderedScrollX, -static_cast<double>(m_lineHeight), textAreaWidth,
                                height() + 2.0 * m_lineHeight));

    /* Selection highlight, drawn behind the glyphs (this loop runs before
     * the text-drawing loop below, same translate/clip) so text stays
     * crisp on top of the translucent overlay — see docs/adr/0019. */
    for (int i = 0; i < m_cursors.size(); ++i) {
        if (!hasSelectionAt(i)) {
            continue;
        }
        highlightRange(painter, selectionMinAt(i), selectionMaxAt(i), firstLine, lastLine, m_selectionColor);
    }

    /* Find/replace matches — same pass, same reasoning. Regular matches
     * reuse the selection color; the current match gets the stronger,
     * dedicated find_match color, drawn last so it lands on top of any
     * regular-match rect it might overlap. See docs/adr/0021. */
    if (!m_matches.isEmpty()) {
        size_t needleLen = static_cast<size_t>(m_findNeedle.size());
        for (int m = 0; m < m_matches.size(); ++m) {
            if (m == m_currentMatch) {
                continue;
            }
            highlightRange(painter, m_matches[m], m_matches[m] + needleLen, firstLine, lastLine, m_selectionColor);
        }
        if (m_currentMatch >= 0) {
            highlightRange(painter, m_matches[m_currentMatch], m_matches[m_currentMatch] + needleLen, firstLine,
                            lastLine, m_findMatchColor);
        }
    }

    for (int line = firstLine; line < lastLine; ++line) {
        int start = m_lineStarts[line];
        int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        int y = (line - firstLine) * m_lineHeight;
        drawLine(painter, start, end, y);
    }
    painter.restore();

    /* Carets are drawn separately, in absolute widget space, at their
     * *rendered* (eased) positions — decoupled from the instant scroll
     * target above so a caret mid-glide isn't forced to jump with it.
     * See docs/adr/0015. */
    /* Both branches key off m_idleTicks (reset to 0 by resetCaretBlink on
     * every cursor-moving action), not a free-running counter — so both
     * "the caret must not blink/fade while the user is actively moving
     * it" for either mode. cos (not sin) means phase 0 — i.e. the instant
     * something goes idle — evaluates to full brightness, so the fade
     * always starts from "was solid, now easing into the breathing
     * cycle" rather than jumping to some arbitrary point in the curve.
     * See docs/adr/0017. */
    int caretAlpha = 255;
    if (m_animationsEnabled) {
        double phase = (m_idleTicks % kCaretAnimationTicks) / static_cast<double>(kCaretAnimationTicks);
        caretAlpha = std::clamp(static_cast<int>(128 + 127 * std::cos(phase * kTwoPi)), 0, 255);
    } else if (!m_caretVisible) {
        caretAlpha = 0;
    }

    if (caretAlpha > 0 && !m_renderedCaretPos.isEmpty()) {
        QColor caretColor = m_textColor;
        caretColor.setAlpha(caretAlpha);
        painter.save();
        painter.setClipRect(QRect(gutter, 0, textAreaWidth, height()));
        for (const QPointF &pos : m_renderedCaretPos) {
            painter.fillRect(QRectF(pos.x(), pos.y(), kCaretWidth, m_lineHeight), caretColor);
        }
        painter.restore();
    }

    if (gutter > 0) {
        painter.save();
        painter.translate(0, -fracLine * m_lineHeight);
        painter.fillRect(QRectF(0, -m_lineHeight, gutter, height() + 2.0 * m_lineHeight), m_backgroundColor);

        int cursorLine = lineForOffset(m_cursors.last());
        QColor dim = m_textColor;
        dim.setAlpha(145); /* reuses the comment-dimming tier — see docs/adr/0014, decision 4 */
        QColor current = m_textColor;
        current.setAlpha(220);

        painter.setFont(m_font);
        for (int line = firstLine; line < lastLine; ++line) {
            painter.setPen(line == cursorLine ? current : dim);
            int y = (line - firstLine) * m_lineHeight;
            painter.drawText(QRect(0, y, gutter - kGutterPadding, m_lineHeight), Qt::AlignRight | Qt::AlignVCenter,
                              gutterLabelForLine(line, cursorLine));
        }
        painter.restore();
    }
}

/* Advances the rendered scroll/caret state one step toward its logical
 * target. Called from the top of paintEvent — see docs/adr/0015. */
void EditorViewport::updateAnimation() {
    if (!m_animationsEnabled) {
        /* Snap every rendered value to its exact target — NOT clear
         * m_renderedCaretPos. paintEvent only draws carets when that
         * array is non-empty, so clearing it here (an earlier bug)
         * meant the caret never rendered at all with the default
         * animations = false config. */
        snapAnimationToTarget();
        return;
    }

    double deltaLine = m_scrollLine - m_renderedScrollLine;
    m_renderedScrollLine += (std::abs(deltaLine) < 0.02) ? deltaLine : deltaLine * kEaseFactor;

    double deltaX = m_scrollX - m_renderedScrollX;
    m_renderedScrollX += (std::abs(deltaX) < 0.5) ? deltaX : deltaX * kEaseFactor;

    if (m_renderedCaretPos.size() != m_cursors.size()) {
        /* Cursor count just changed (added/removed) — snap to targets
         * this frame rather than gliding from a mismatched old array. */
        m_renderedCaretPos.resize(m_cursors.size());
        for (int i = 0; i < m_cursors.size(); ++i) {
            m_renderedCaretPos[i] = caretTargetFor(m_cursors[i]);
        }
        return;
    }

    for (int i = 0; i < m_cursors.size(); ++i) {
        QPointF target = caretTargetFor(m_cursors[i]);
        QPointF &rendered = m_renderedCaretPos[i];
        QPointF delta = target - rendered;
        if (std::abs(delta.x()) < 0.5 && std::abs(delta.y()) < 0.5) {
            rendered = target;
        } else {
            rendered += delta * kEaseFactor;
        }
    }
}

QPointF EditorViewport::caretTargetFor(size_t cursor) const {
    int line = lineForOffset(cursor);
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForOffset(cursor, line);
    double x = gutterWidth() - m_renderedScrollX + xForColumn(lineStart, lineEnd, col);
    double y = (line - m_renderedScrollLine) * m_lineHeight;
    return QPointF(x, y);
}

void EditorViewport::resetCaretBlink() {
    m_caretVisible = true;
    m_idleTicks = 0;
}

void EditorViewport::snapAnimationToTarget() {
    m_renderedScrollLine = m_scrollLine;
    m_renderedScrollX = m_scrollX;
    m_renderedCaretPos.resize(m_cursors.size());
    for (int i = 0; i < m_cursors.size(); ++i) {
        m_renderedCaretPos[i] = caretTargetFor(m_cursors[i]);
    }
}

/* One entry per byte in [start, end), naming which capture (if any) that
 * byte belongs to — shared by drawLine and xForColumn so both segment a
 * line into runs identically. */
QVector<AseHighlightCapture> EditorViewport::capturesForLine(int start, int end) const {
    int lineLen = end - start;
    QVector<AseHighlightCapture> captures(std::max(0, lineLen), ASE_HL_NONE);

    for (const AseHighlightSpan &span : m_highlights) {
        int spanStart = static_cast<int>(span.start);
        int spanEnd = static_cast<int>(span.end);
        if (spanEnd <= start || spanStart >= end) {
            continue;
        }
        int clampedStart = std::max(spanStart, start);
        int clampedEnd = std::min(spanEnd, end);
        for (int i = clampedStart; i < clampedEnd; ++i) {
            captures[i - start] = span.capture;
        }
    }
    return captures;
}

/* Splits [start, end) into same-capture runs and draws each with its own
 * style. All captures render in m_textColor's hue — only opacity/weight/style
 * vary — so the query's captures don't need to be mutually exclusive in
 * general, just non-overlapping in practice for the leaf-level nodes
 * c_highlights.scm captures. See docs/adr/0007, decision 1.
 *
 * Each run's on-screen advance is that run's own *measured* rendered
 * width (QFontMetrics::horizontalAdvance on the actual run text, with
 * that run's actual font), not runLen * m_charWidth — see docs/adr/0013
 * for why the latter drifts visibly on longer lines.
 *
 * The drawText rect's width is that same measured runWidth, never
 * width() - x. Once horizontal scroll is in play (docs/adr/0014), x is
 * a *local* (translated) coordinate that can legitimately exceed the
 * widget's raw width() — width() - x then goes negative, and a
 * negative-width QRect makes drawText paint nothing. That was a real
 * bug: characters typed past the point a line had scrolled rendered as
 * blank space. See docs/adr/0017. */
void EditorViewport::drawLine(QPainter &painter, int start, int end, int y) {
    int lineLen = end - start;
    if (lineLen <= 0) {
        return;
    }

    QVector<AseHighlightCapture> captures = capturesForLine(start, end);

    int x = 0;
    int runStart = 0;
    for (int i = 1; i <= lineLen; ++i) {
        if (i < lineLen && captures[i] == captures[runStart]) {
            continue;
        }
        int runLen = i - runStart;
        QString text = QString::fromUtf8(m_cache.constData() + start + runStart, runLen);
        AseHighlightCapture capture = captures[runStart];
        int runWidth = metricsForCapture(capture).horizontalAdvance(text);
        painter.setFont(fontForCapture(capture));
        painter.setPen(colorForCapture(capture));
        painter.drawText(QRect(x, y, runWidth, m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter, text);
        x += runWidth;
        runStart = i;
    }
}

/* Same run segmentation as drawLine, stopping at `column` instead of the
 * end of the line — so the caret lands exactly where drawLine actually
 * painted the character before it, even mid-run or mid-bold/italic-run.
 * See docs/adr/0013. */
int EditorViewport::xForColumn(int lineStart, int lineEnd, int column) const {
    int lineLen = lineEnd - lineStart;
    column = std::clamp(column, 0, lineLen);
    if (column == 0) {
        return 0;
    }

    QVector<AseHighlightCapture> captures = capturesForLine(lineStart, lineEnd);

    int x = 0;
    int runStart = 0;
    for (int i = 1; i <= column; ++i) {
        if (i < column && captures[i] == captures[runStart]) {
            continue;
        }
        int runLen = i - runStart;
        QString text = QString::fromUtf8(m_cache.constData() + lineStart + runStart, runLen);
        x += metricsForCapture(captures[runStart]).horizontalAdvance(text);
        runStart = i;
    }
    return x;
}

/* Shared by the selection pass and the find/replace-match pass — one
 * rect per visual line in [firstLine, lastLine) that [start, end)
 * touches, using the same xForColumn measurement text/carets use. See
 * docs/adr/0019, docs/adr/0021. */
void EditorViewport::highlightRange(QPainter &painter, size_t start, size_t end, int firstLine, int lastLine,
                                     const QColor &color) const {
    if (start >= end) {
        return;
    }
    int startLine = lineForOffset(start);
    int endLine = lineForOffset(end);
    for (int line = std::max(firstLine, startLine); line <= std::min(lastLine - 1, endLine); ++line) {
        int lineStart = m_lineStarts[line];
        int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        int rangeStartCol = (line == startLine) ? static_cast<int>(start) - lineStart : 0;
        int rangeEndCol = (line == endLine) ? static_cast<int>(end) - lineStart : lineEnd - lineStart;
        int x0 = xForColumn(lineStart, lineEnd, rangeStartCol);
        int x1 = xForColumn(lineStart, lineEnd, rangeEndCol);
        int y = (line - firstLine) * m_lineHeight;
        int rectWidth = x1 - x0;
        if (line < endLine) {
            rectWidth += m_charWidth / 2; /* hints the range's line break continues */
        }
        painter.fillRect(QRectF(x0, y, std::max(1, rectWidth), m_lineHeight), color);
    }
}

int EditorViewport::gutterWidth() const {
    if (m_lineNumberMode == QLatin1String("off")) {
        return 0;
    }
    /* Measured from the actual widest label, not digit-count * a fixed
     * per-digit width — same reasoning as the caret-drift fix, applied
     * to new code. See docs/adr/0014, decision 4. */
    QString widest = QString::number(std::max(1, static_cast<int>(m_lineStarts.size())));
    int textWidth = QFontMetrics(m_font).horizontalAdvance(widest);
    return textWidth + kGutterPadding * 2;
}

QString EditorViewport::gutterLabelForLine(int line, int cursorLine) const {
    if (m_lineNumberMode == QLatin1String("relative") && line != cursorLine) {
        return QString::number(std::abs(line - cursorLine));
    }
    return QString::number(line + 1); /* 1-based display */
}

QFont EditorViewport::fontForCapture(AseHighlightCapture capture) const {
    QFont font = m_font;
    if (capture == ASE_HL_KEYWORD) {
        font.setBold(true);
    } else if (capture == ASE_HL_TYPE) {
        font.setItalic(true);
    }
    return font;
}

const QFontMetrics &EditorViewport::metricsForCapture(AseHighlightCapture capture) const {
    if (capture == ASE_HL_KEYWORD) {
        return m_boldMetrics;
    }
    if (capture == ASE_HL_TYPE) {
        return m_italicMetrics;
    }
    return m_metrics;
}

QColor EditorViewport::colorForCapture(AseHighlightCapture capture) const {
    QColor color = m_textColor;
    switch (capture) {
    case ASE_HL_STRING:
    case ASE_HL_NUMBER:
        color.setAlpha(200);
        break;
    case ASE_HL_COMMENT:
        /* 145/255 (~57%), not the original 115/255 (~45%) — that measured
         * 3.64:1 against the background, below WCAG AA's 4.5:1 for normal
         * text. See docs/adr/0012, decision 4. */
        color.setAlpha(145);
        break;
    case ASE_HL_KEYWORD:
    case ASE_HL_TYPE:
    case ASE_HL_NONE:
    default:
        break;
    }
    return color;
}

void EditorViewport::keyPressEvent(QKeyEvent *event) {
    resetCaretBlink();

    bool extend = event->modifiers() & Qt::ShiftModifier;

    if (event->key() == Qt::Key_Up) {
        moveCursorVertically(-1, extend);
        ensureCursorVisible();
        update();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        moveCursorVertically(1, extend);
        ensureCursorVisible();
        update();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        collapseToOneCursor();
        ensureCursorVisible();
        update();
        return;
    }
    m_desiredColumn = -1;
    /* Set only for plain character insertion — see
     * snapAnimationToTarget's doc comment (docs/adr/0017): typing
     * always renders instantly, even with animations on, because
     * gliding can't keep pace with fast repeated small jumps and the
     * result reads as lag, not smoothness. Enter and Backspace/Delete
     * are deliberately *not* in that bucket (see their own cases
     * below, and docs/adr/0028) — each is a single, discrete cursor
     * jump rather than a rapid sequence, so it doesn't have that
     * problem and gets to glide like navigation does. */
    bool isEdit = false;

    switch (event->key()) {
    case Qt::Key_Left:
        moveCursorLeft(extend);
        break;
    case Qt::Key_Right:
        moveCursorRight(extend);
        break;
    case Qt::Key_Home:
        moveCursorHome(extend);
        break;
    case Qt::Key_End:
        moveCursorEnd(extend);
        break;
    case Qt::Key_Backspace:
        /* Not isEdit = true, matching Enter — see docs/adr/0028 and
         * the comment on the Return/Enter case below. Deleting a
         * newline (merging two lines) is the same kind of discrete
         * jump Enter makes, worth gliding; deleting an ordinary
         * character is a one-column move, animated or not. */
        deleteBackward();
        break;
    case Qt::Key_Delete:
        deleteForward();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        /* Deliberately *not* isEdit = true — see docs/adr/0027 and
         * docs/adr/0028. A newline is a single, discrete jump to a new
         * line, closer in feel to navigation than to character-by-
         * character typing, and it's the first edit the user asked to
         * see glide (Backspace/Delete followed once deletion was
         * asked for too). Regular character insertion stays instant —
         * ADR 0017's original reasoning (gliding can't keep pace with
         * fast repeated small jumps) still holds for that case. */
        insertText(QByteArrayLiteral("\n"));
        break;
    default:
        if (event->modifiers() & Qt::ControlModifier) {
            if (event->key() == Qt::Key_S) {
                /* Ctrl+Shift+S always opens Save-As, even with a path
                 * already set — "save as" means "let me pick a
                 * different one," not "save.". Ctrl+S with no path set
                 * falls through to save()'s own Save-As fallback. */
                if (event->modifiers() & Qt::ShiftModifier) {
                    if (m_fileBrowser != nullptr) {
                        m_fileBrowser->openFor(FileBrowserPanel::Mode::SaveAs);
                    }
                } else {
                    save();
                }
                return;
            }
            if (event->key() == Qt::Key_O) {
                /* Ctrl+Shift+O toggles the output panel directly,
                 * without going through :output — Ctrl+O (no Shift)
                 * keeps its existing meaning, Open. */
                if (event->modifiers() & Qt::ShiftModifier) {
                    toggleOutputPanel();
                } else if (m_fileBrowser != nullptr) {
                    m_fileBrowser->openFor(FileBrowserPanel::Mode::Open);
                }
                return;
            }
            if (event->key() == Qt::Key_Semicolon) {
                /* Command-line trigger — Ctrl+; here, not a bare `:`
                 * (that's the ex-command-line convention Vim mode will
                 * use later; in normal mode a bare `:` has to stay a
                 * literal, typeable character). See docs/adr/0025. */
                if (m_commandLine != nullptr) {
                    m_commandLine->openCommandLine();
                }
                return;
            }
            if (event->key() == Qt::Key_B) {
                compile();
                return;
            }
            if (event->key() == Qt::Key_Q) {
                window()->close();
                return;
            }
            if (event->key() == Qt::Key_Slash) {
                if (m_helpPanel != nullptr) {
                    m_helpPanel->openHelp();
                }
                return;
            }
            if (event->key() == Qt::Key_I) {
                if (m_aboutPanel != nullptr) {
                    m_aboutPanel->openAbout();
                }
                return;
            }
            if (event->key() == Qt::Key_D) {
                addCursorAtNextOccurrence();
                return;
            }
            if (event->key() == Qt::Key_A) {
                selectAll();
                return;
            }
            if (event->key() == Qt::Key_C) {
                copySelection();
                return;
            }
            if (event->key() == Qt::Key_X) {
                cutSelection();
                return;
            }
            if (event->key() == Qt::Key_V) {
                pasteClipboard();
                return;
            }
            if (event->key() == Qt::Key_F) {
                if (m_findBar != nullptr) {
                    m_findBar->openFor(FindBar::Mode::Find);
                }
                return;
            }
            if (event->key() == Qt::Key_H) {
                if (m_findBar != nullptr) {
                    m_findBar->openFor(FindBar::Mode::Replace);
                }
                return;
            }
            if (event->key() == Qt::Key_Z) {
                if (event->modifiers() & Qt::ShiftModifier) {
                    redo();
                } else {
                    undo();
                }
                return;
            }
            QWidget::keyPressEvent(event);
            return;
        }

        {
            const QString text = event->text();
            if (text.isEmpty() || !text.at(0).isPrint()) {
                QWidget::keyPressEvent(event);
                return;
            }
            insertText(text.toUtf8());
            isEdit = true;
        }
    }

    ensureCursorVisible();
    if (isEdit) {
        snapAnimationToTarget();
    }
    update();
}

void EditorViewport::wheelEvent(QWheelEvent *event) {
    int lines = event->angleDelta().y() / 40;
    int maxScroll = std::max(0, static_cast<int>(m_lineStarts.size()) - 1);
    m_scrollLine = std::clamp(m_scrollLine - lines, 0, maxScroll);
    update();
}

void EditorViewport::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    size_t offset = offsetForPoint(event->position().toPoint());

    if (event->modifiers() & Qt::ShiftModifier) {
        /* Extends the primary (last) cursor's selection to the click
         * point instead of clearing — its anchor is left untouched, so a
         * fresh Shift+click after a plain click anchors at the old
         * cursor position. Multi-cursor + Shift+click isn't a scoped
         * combination (drag-selection is single-cursor-only too, see
         * mouseMoveEvent) — only the last cursor is affected. */
        if (!m_cursors.isEmpty()) {
            m_cursors.last() = offset;
        }
    } else if (event->modifiers() & Qt::AltModifier) {
        m_cursors.push_back(offset);
        m_selectionAnchors.push_back(offset);
        normalizeCursors();
    } else {
        m_cursors.clear();
        m_selectionAnchors.clear();
        m_cursors.push_back(offset);
        m_selectionAnchors.push_back(offset);
    }

    m_desiredColumn = -1;
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

/* While the left button is held, Qt keeps delivering move events to this
 * widget (implicit press-grab) regardless of setMouseTracking — no extra
 * grab needed. Only extends the single-cursor case: a plain press already
 * collapsed to one cursor, so a drag starting from a multi-cursor state
 * can't happen. See docs/adr/0019. */
void EditorViewport::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton) || m_cursors.size() != 1) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    m_cursors[0] = offsetForPoint(event->position().toPoint());
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

void EditorViewport::normalizeCursors() {
    QVector<size_t> anchors = m_selectionAnchors;
    QVector<int> order(m_cursors.size());
    for (int i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [this](int a, int b) { return m_cursors[a] < m_cursors[b]; });

    QVector<size_t> sortedCursors;
    QVector<size_t> sortedAnchors;
    sortedCursors.reserve(order.size());
    sortedAnchors.reserve(order.size());
    for (int idx : order) {
        if (!sortedCursors.isEmpty() && sortedCursors.last() == m_cursors[idx]) {
            continue; /* de-dupe by cursor position; keep the first anchor seen */
        }
        sortedCursors.push_back(m_cursors[idx]);
        sortedAnchors.push_back(anchors[idx]);
    }

    m_cursors = std::move(sortedCursors);
    m_selectionAnchors = std::move(sortedAnchors);
    if (m_cursors.isEmpty()) {
        m_cursors.push_back(0);
        m_selectionAnchors.push_back(0);
    }
}

void EditorViewport::collapseToOneCursor() {
    if (m_cursors.size() <= 1) {
        m_selectionAnchors[0] = m_cursors[0]; /* Escape also clears an active selection */
        return;
    }
    size_t keep = m_cursors.last();
    m_cursors.clear();
    m_selectionAnchors.clear();
    m_cursors.push_back(keep);
    m_selectionAnchors.push_back(keep);
}

/* "Select next occurrence" (Ctrl+D, Sublime/VS Code convention) without a
 * selection-range model: adds a point cursor at the end of the next
 * whole-word match after the word at the last cursor. Doesn't wrap. */
void EditorViewport::addCursorAtNextOccurrence() {
    if (m_cursors.isEmpty()) {
        return;
    }
    size_t anchor = m_cursors.last();
    int len = m_cache.size();

    int wordStart = static_cast<int>(anchor);
    while (wordStart > 0 && isWordChar(m_cache[wordStart - 1])) {
        wordStart--;
    }
    int wordEnd = static_cast<int>(anchor);
    while (wordEnd < len && isWordChar(m_cache[wordEnd])) {
        wordEnd++;
    }
    if (wordStart == wordEnd) {
        return; /* anchor isn't touching a word */
    }

    QByteArray word = m_cache.mid(wordStart, wordEnd - wordStart);
    int wordLen = word.size();

    for (int searchStart = wordEnd; searchStart + wordLen <= len; ++searchStart) {
        if (m_cache.mid(searchStart, wordLen) != word) {
            continue;
        }
        bool boundaryBefore = (searchStart == 0) || !isWordChar(m_cache[searchStart - 1]);
        bool boundaryAfter = (searchStart + wordLen == len) || !isWordChar(m_cache[searchStart + wordLen]);
        if (boundaryBefore && boundaryAfter) {
            size_t newCursor = static_cast<size_t>(searchStart + wordLen);
            m_cursors.push_back(newCursor);
            m_selectionAnchors.push_back(newCursor);
            normalizeCursors();
            ensureCursorVisible();
            update();
            return;
        }
    }
    /* no further occurrence forward — no-op, see docs/adr/0012 */
}

/* Collapses to one cursor with a selection spanning the whole buffer —
 * anchor at the start, head at the end, so it composes with everything
 * else that already treats "a selection" as just m_selectionAnchors[i]
 * != m_cursors[i] (copy, delete, replace-over-selection, ...). See
 * docs/adr/0028. */
void EditorViewport::selectAll() {
    m_cursors = {static_cast<size_t>(m_cache.size())};
    m_selectionAnchors = {0};
    m_desiredColumn = -1;
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

bool EditorViewport::hasSelectionAt(int i) const {
    return m_selectionAnchors[i] != m_cursors[i];
}

size_t EditorViewport::selectionMinAt(int i) const {
    return std::min(m_cursors[i], m_selectionAnchors[i]);
}

size_t EditorViewport::selectionMaxAt(int i) const {
    return std::max(m_cursors[i], m_selectionAnchors[i]);
}

/* Reads the about-to-be-deleted range out of m_cache before deleting, same
 * reasoning as deleteBackwardAt/deleteForwardAt below (see docs/adr/0018)
 * — safe under the batch's highest-offset-first processing order. */
void EditorViewport::deleteSelectionAt(int i) {
    size_t start = selectionMinAt(i);
    size_t end = selectionMaxAt(i);
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    m_cursors[i] = start;
    m_selectionAnchors[i] = start;
}

/* Reads straight out of m_cache rather than ase_buffer_get_text — it's
 * already a full, current mirror of the buffer (ADR 0006). Multiple
 * selections join with '\n', the standard multi-cursor copy convention.
 * See docs/adr/0020. */
bool EditorViewport::copySelection() {
    QStringList parts;
    for (int i = 0; i < m_cursors.size(); ++i) {
        if (!hasSelectionAt(i)) {
            continue;
        }
        size_t start = selectionMinAt(i);
        size_t end = selectionMaxAt(i);
        parts.push_back(
            QString::fromUtf8(m_cache.constData() + static_cast<int>(start), static_cast<int>(end - start)));
    }
    if (parts.isEmpty()) {
        return false;
    }
    QGuiApplication::clipboard()->setText(parts.join(QLatin1Char('\n')));
    return true;
}

/* Copy, then delete every selection as one undo group — reuses the
 * selection-delete path Phase 11 added. A no-op (clipboard untouched)
 * when nothing is selected anywhere. */
void EditorViewport::cutSelection() {
    if (!copySelection()) {
        return;
    }
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        if (hasSelectionAt(i)) {
            deleteSelectionAt(i);
        }
    }
    normalizeCursors();
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

/* Inserts the same clipboard text at every cursor (reusing insertText's
 * existing multi-cursor broadcast and selection-replace semantics) rather
 * than distributing clipboard lines one-per-cursor — a real feature some
 * editors have, but not worth the added complexity for v1. */
void EditorViewport::pasteClipboard() {
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
        return;
    }
    insertText(text.toUtf8());
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

QString EditorViewport::primarySelectionText() const {
    int i = m_cursors.size() - 1;
    if (i < 0 || !hasSelectionAt(i)) {
        return QString();
    }
    size_t start = selectionMinAt(i);
    size_t end = selectionMaxAt(i);
    return QString::fromUtf8(m_cache.constData() + static_cast<int>(start), static_cast<int>(end - start));
}

/* Plain substring, ASCII-case-insensitive: fresh QByteArray::toLower()
 * copies of both needle and haystack every call — cheap enough at this
 * project's scale, and toLower() is byte-wise ASCII-only, so it never
 * changes a match's byte length (non-ASCII bytes pass through
 * unchanged). Matches are non-overlapping. Doesn't move the cursor or
 * touch m_currentMatch's *value* beyond clamping it back into range —
 * callers (setFindQuery, findNext/Previous, replace*) decide whether to
 * actually jump. See docs/adr/0021. */
void EditorViewport::recomputeMatches() {
    m_matches.clear();
    if (m_findNeedle.isEmpty()) {
        m_currentMatch = -1;
        return;
    }

    QByteArray needleLower = m_findNeedle.toLower();
    QByteArray hayLower = m_cache.toLower();
    int needleLen = needleLower.size();
    int pos = 0;
    while (true) {
        int idx = hayLower.indexOf(needleLower, pos);
        if (idx < 0) {
            break;
        }
        m_matches.push_back(static_cast<size_t>(idx));
        pos = idx + needleLen;
    }

    if (m_currentMatch >= m_matches.size()) {
        m_currentMatch = m_matches.isEmpty() ? -1 : m_matches.size() - 1;
    }
}

int EditorViewport::nearestMatchAtOrAfter(size_t offset) const {
    for (int i = 0; i < m_matches.size(); ++i) {
        if (m_matches[i] >= offset) {
            return i;
        }
    }
    return 0; /* nothing at/after offset — wrap to the first match */
}

/* Selects m_matches[index] (wrapping either direction) like any other
 * selection, so replaceCurrentMatch can just reuse insertText's existing
 * selection-replace path instead of its own delete/insert logic. */
void EditorViewport::jumpToMatch(int index) {
    if (m_matches.isEmpty()) {
        return;
    }
    m_currentMatch = ((index % m_matches.size()) + m_matches.size()) % m_matches.size();
    size_t start = m_matches[m_currentMatch];
    size_t end = start + static_cast<size_t>(m_findNeedle.size());
    m_cursors = {end};
    m_selectionAnchors = {start};
    m_desiredColumn = -1;
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

/* Recomputes the match list for `needle` and, if anything matches, jumps
 * to the nearest one at/after the current cursor (wrapping to the first
 * match otherwise) — the usual "start typing, land on the nearest hit"
 * incremental-search feel. Called on every keystroke in FindBar's find
 * field. */
void EditorViewport::setFindQuery(const QString &needle) {
    m_findNeedle = needle.toUtf8();
    recomputeMatches();
    if (!m_matches.isEmpty()) {
        jumpToMatch(nearestMatchAtOrAfter(m_cursors.isEmpty() ? 0 : m_cursors[0]));
    } else {
        m_currentMatch = -1;
        update();
    }
}

void EditorViewport::clearFindQuery() {
    m_findNeedle.clear();
    m_matches.clear();
    m_currentMatch = -1;
    update();
}

void EditorViewport::findNext() {
    if (m_matches.isEmpty()) {
        return;
    }
    jumpToMatch(m_currentMatch < 0 ? 0 : m_currentMatch + 1);
}

void EditorViewport::findPrevious() {
    if (m_matches.isEmpty()) {
        return;
    }
    jumpToMatch(m_currentMatch < 0 ? -1 : m_currentMatch - 1);
}

/* The current match is already selected (jumpToMatch put it there), so
 * this is just insertText over an active selection — same path typing
 * over any other selection takes, including its undo group. Advances to
 * whatever match now sits at/after the replacement point. */
void EditorViewport::replaceCurrentMatch(const QByteArray &replacement) {
    if (m_currentMatch < 0 || m_matches.isEmpty()) {
        return;
    }
    insertText(replacement);
    if (!m_matches.isEmpty()) {
        jumpToMatch(nearestMatchAtOrAfter(m_cursors.isEmpty() ? 0 : m_cursors[0]));
    } else {
        m_currentMatch = -1;
        ensureCursorVisible();
    }
    snapAnimationToTarget();
    update();
}

/* Every match, highest-offset-first, as one undo group — the same
 * discipline every other multi-offset batch edit in this file already
 * follows (ADR 0012, ADR 0018). Unlike insertTextAt's selection-replace
 * reuse, this writes directly through ase_buffer_delete/_insert since
 * there's no per-cursor selection here, just a flat offset list. Leaves
 * the cursor at the start of the buffer — v1 simplification, documented
 * in docs/adr/0021, rather than tracking where the "same" text ended up
 * post-replacement. */
void EditorViewport::replaceAllMatches(const QByteArray &replacement) {
    if (m_matches.isEmpty()) {
        return;
    }
    size_t needleLen = static_cast<size_t>(m_findNeedle.size());
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_matches.size() - 1; i >= 0; --i) {
        size_t start = m_matches[i];
        QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(needleLen));
        if (!ase_buffer_delete(m_buffer, start, needleLen)) {
            continue;
        }
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
        if (!replacement.isEmpty() &&
            ase_buffer_insert(m_buffer, start, replacement.constData(), static_cast<size_t>(replacement.size()))) {
            ase_undo_record_insert(m_undo, start, replacement.constData(), static_cast<size_t>(replacement.size()));
        }
    }
    m_cursors = {0};
    m_selectionAnchors = {0};
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache(); /* recomputes m_matches too */
    m_currentMatch = m_matches.isEmpty() ? -1 : 0;
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

void EditorViewport::insertText(const QByteArray &bytes) {
    if (bytes.isEmpty()) {
        return;
    }
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        insertTextAt(i, bytes);
    }
    normalizeCursors();
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
}

/* An active selection is replaced: delete it first (as part of the same
 * undo group), then insert at the collapse point — see docs/adr/0019. */
void EditorViewport::insertTextAt(int i, const QByteArray &bytes) {
    if (hasSelectionAt(i)) {
        deleteSelectionAt(i);
    }
    size_t &cursor = m_cursors[i];
    if (ase_buffer_insert(m_buffer, cursor, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, cursor, bytes.constData(), static_cast<size_t>(bytes.size()));
        cursor += static_cast<size_t>(bytes.size());
    }
    m_selectionAnchors[i] = cursor;
}

void EditorViewport::deleteBackward() {
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        deleteBackwardAt(i);
    }
    normalizeCursors();
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
}

/* m_cache still mirrors the buffer as it stood *before this whole batch*
 * (refreshCache() only runs once, after every cursor in the loop has
 * been processed) — but reading the about-to-be-deleted bytes out of it
 * here is still correct: cursors are processed highest-offset-first, so
 * by the time this particular cursor's [start, cursor) range is touched,
 * no earlier step in the loop could have written into it (only ranges at
 * or above this cursor's own offset could have moved, per the same
 * invariant that already lets deleteBackwardAt skip cross-cursor
 * bookkeeping — see docs/adr/0012). See docs/adr/0018 for why the undo
 * stack needs this snapshot at all: ase_buffer_delete doesn't hand back
 * what it removed. An active selection is the whole operation — no extra
 * character is removed beyond it — see docs/adr/0019. */
void EditorViewport::deleteBackwardAt(int i) {
    if (hasSelectionAt(i)) {
        deleteSelectionAt(i);
        return;
    }
    size_t &cursor = m_cursors[i];
    if (cursor == 0) {
        return;
    }
    size_t start = cursor - 1;
    while (start > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(start)])) {
        start--;
    }
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(cursor - start));
    if (ase_buffer_delete(m_buffer, start, cursor - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
        cursor = start;
    }
    m_selectionAnchors[i] = cursor;
}

void EditorViewport::deleteForward() {
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        deleteForwardAt(i);
    }
    normalizeCursors();
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
}

void EditorViewport::deleteForwardAt(int i) {
    if (hasSelectionAt(i)) {
        deleteSelectionAt(i);
        return;
    }
    size_t &cursor = m_cursors[i];
    size_t len = static_cast<size_t>(m_cache.size());
    if (cursor >= len) {
        return;
    }
    size_t end = cursor + 1;
    while (end < len && isUtf8ContinuationByte(m_cache[static_cast<int>(end)])) {
        end++;
    }
    QByteArray removed = m_cache.mid(static_cast<int>(cursor), static_cast<int>(end - cursor));
    if (ase_buffer_delete(m_buffer, cursor, end - cursor)) {
        ase_undo_record_delete(m_undo, cursor, removed.constData(), static_cast<size_t>(removed.size()));
    }
    m_selectionAnchors[i] = cursor;
}

void EditorViewport::moveCursorLeft(bool extend) {
    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorLeftAt(i, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorLeftAt(int i, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = selectionMinAt(i);
    } else if (cursor > 0) {
        size_t pos = cursor - 1;
        while (pos > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
            pos--;
        }
        cursor = pos;
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::moveCursorRight(bool extend) {
    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorRightAt(i, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorRightAt(int i, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = selectionMaxAt(i);
    } else {
        size_t len = static_cast<size_t>(m_cache.size());
        if (cursor < len) {
            size_t pos = cursor + 1;
            while (pos < len && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
                pos++;
            }
            cursor = pos;
        }
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::moveCursorVertically(int lineDelta, bool extend) {
    if (m_cursors.size() == 1) {
        /* sticky column — see docs/adr/0012, decision 1 */
        size_t &cursor = m_cursors[0];
        if (!extend && hasSelectionAt(0)) {
            cursor = (lineDelta < 0) ? selectionMinAt(0) : selectionMaxAt(0);
            m_selectionAnchors[0] = cursor;
            m_desiredColumn = -1;
            return;
        }

        int line = lineForOffset(cursor);
        int column = (m_desiredColumn >= 0) ? m_desiredColumn : columnForOffset(cursor, line);
        m_desiredColumn = column;

        int newLine = line + lineDelta;
        if (newLine >= 0 && newLine < m_lineStarts.size()) {
            cursor = offsetForLineColumn(newLine, column);
        }
        if (!extend) {
            m_selectionAnchors[0] = cursor;
        }
        return;
    }

    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorVerticallyAt(i, lineDelta, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorVerticallyAt(int i, int lineDelta, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = (lineDelta < 0) ? selectionMinAt(i) : selectionMaxAt(i);
    } else {
        int line = lineForOffset(cursor);
        int column = columnForOffset(cursor, line);
        int newLine = line + lineDelta;
        if (newLine >= 0 && newLine < m_lineStarts.size()) {
            cursor = offsetForLineColumn(newLine, column);
        }
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::moveCursorHome(bool extend) {
    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorHomeAt(i, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorHomeAt(int i, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = selectionMinAt(i);
    } else {
        int line = lineForOffset(cursor);
        cursor = static_cast<size_t>(m_lineStarts[line]);
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::moveCursorEnd(bool extend) {
    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorEndAt(i, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorEndAt(int i, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = selectionMaxAt(i);
    } else {
        int line = lineForOffset(cursor);
        int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        cursor = static_cast<size_t>(end);
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::ensureCursorVisible() {
    size_t cursor = m_cursors.last();
    int line = lineForOffset(cursor);
    int visibleLines = std::max(1, height() / m_lineHeight);
    /* Clamped so the margin can never exceed half the viewport — a
     * short/narrow window degrades to less context instead of
     * oscillating or refusing to scroll. See docs/adr/0024. */
    int vMargin = std::min(kVerticalScrollMargin, std::max(0, (visibleLines - 1) / 2));
    if (line < m_scrollLine + vMargin) {
        m_scrollLine = line - vMargin;
    } else if (line >= m_scrollLine + visibleLines - vMargin) {
        m_scrollLine = line - visibleLines + 1 + vMargin;
    }
    m_scrollLine = std::max(0, m_scrollLine);

    /* Horizontal half, symmetric to the vertical logic above — see
     * docs/adr/0014, decision 3. */
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForOffset(cursor, line);
    int caretX = xForColumn(lineStart, lineEnd, col);

    int textAreaWidth = std::max(1, width() - gutterWidth());
    int hMargin = std::min(m_charWidth * kHorizontalScrollMarginChars, std::max(0, (textAreaWidth - kCaretWidth) / 2));
    if (caretX < m_scrollX + hMargin) {
        m_scrollX = caretX - hMargin;
    } else if (caretX + kCaretWidth > m_scrollX + textAreaWidth - hMargin) {
        m_scrollX = caretX + kCaretWidth - textAreaWidth + hMargin;
    }
    m_scrollX = std::max(0, m_scrollX);

    /* 1-based for display — every cursor move and every edit already
     * ends up here, so this is the one place status needs wiring. See
     * docs/adr/0023. */
    emit statusChanged(line + 1, col + 1, m_dirty);
}

/* Empty m_filePath (launched with no file, or a fresh openFile that
 * failed to read one) routes to the Save-As panel instead of silently
 * doing nothing — the one gap ADR 0006 flagged as deferred. */
void EditorViewport::save() {
    if (m_filePath.isEmpty()) {
        if (m_fileBrowser != nullptr) {
            m_fileBrowser->openFor(FileBrowserPanel::Mode::SaveAs);
        }
        return;
    }
    if (ase_buffer_save_to_file(m_buffer, m_filePath.toUtf8().constData())) {
        m_dirty = false;
        ensureCursorVisible(); /* pushes the cleared dirty flag (and title) through statusChanged */
    }
}

/* Destroys the current buffer/syntax/undo-history and loads `path`
 * fresh, resetting every piece of per-buffer state — cursors, scroll,
 * find query, dirty flag. Missing/unreadable files start empty with
 * `path` kept as the save target, same tolerance
 * ase_buffer_create_from_file's caller in main.cpp already had for the
 * initial launch (docs/adr/0006) — opening a not-yet-existing file by
 * name is a normal editor action, not an error. */
void EditorViewport::openFile(const QString &path) {
    ase_syntax_destroy(m_syntax);
    m_syntax = nullptr;
    ase_undo_destroy(m_undo);
    ase_buffer_destroy(m_buffer);

    AseBuffer *buffer = ase_buffer_create_from_file(path.toUtf8().constData());
    if (buffer == nullptr) {
        buffer = ase_buffer_create();
    }
    m_buffer = buffer;
    m_undo = ase_undo_create();
    m_filePath = path;

    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("c") || suffix == QLatin1String("h")) {
        m_syntax = ase_syntax_create_c();
    }

    m_cursors = {0};
    m_selectionAnchors = {0};
    m_scrollLine = 0;
    m_scrollX = 0;
    m_desiredColumn = -1;
    clearFindQuery();
    m_dirty = false;

    refreshCache();
    ensureCursorVisible();
    resetCaretBlink();
    snapAnimationToTarget();
    update();
}

/* Sets the save target then defers to save() itself, so dirty-clearing
 * and the statusChanged emit (title included, via filePath()) happen
 * in exactly one place rather than being duplicated here. */
void EditorViewport::saveAs(const QString &path) {
    m_filePath = path;
    save();
}

void EditorViewport::runCommand(const QString &command) {
    QString trimmed = command.trimmed();
    if (trimmed == QLatin1String("w")) {
        save();
    } else if (trimmed == QLatin1String("q")) {
        window()->close();
    } else if (trimmed == QLatin1String("compile")) {
        compile();
    } else if (trimmed == QLatin1String("output")) {
        toggleOutputPanel();
    }
    /* Anything else: silent no-op — see docs/adr/0025. */
}

void EditorViewport::toggleOutputPanel() {
    if (m_outputPanel != nullptr) {
        m_outputPanel->setVisible(!m_outputPanel->isVisible());
    }
}

/* Reads build_command fresh from config on every call (not cached) so
 * an edited config.ase takes effect on the next :compile without a
 * restart, same hot-reload spirit as everything else config-driven in
 * this class. */
void EditorViewport::compile() {
    if (m_outputPanel == nullptr) {
        return;
    }
    if (m_compileProcess != nullptr) {
        m_outputPanel->appendLine(QStringLiteral("A build is already running."));
        m_outputPanel->show();
        return;
    }

    const char *buildCommand = ase_config_get_string(m_config, "build_command");
    if (buildCommand == nullptr) {
        m_outputPanel->appendLine(QStringLiteral("No build_command configured — see config.ase."));
        m_outputPanel->show();
        return;
    }
    if (m_filePath.isEmpty()) {
        m_outputPanel->appendLine(QStringLiteral("No file to compile — save it first."));
        m_outputPanel->show();
        return;
    }

    QString substituted = QString::fromUtf8(buildCommand).replace(QLatin1String("%f"), m_filePath);
    QByteArray substitutedUtf8 = substituted.toUtf8();
    QByteArray cwdUtf8 = QFileInfo(m_filePath).absolutePath().toUtf8();

    /* Run through a shell, not execvp'd directly — build_command is
     * documented (config.c's starter template) as a shell command, so
     * it can use `&&`/pipes/etc., the same way :compile's config-key
     * comment shows. */
    const char *argv[] = {"/bin/sh", "-c", substitutedUtf8.constData(), nullptr};
    m_compileProcess = ase_process_spawn(argv, cwdUtf8.constData());

    m_outputPanel->clear();
    m_outputPanel->show();
    if (m_compileProcess == nullptr) {
        m_outputPanel->appendLine(QStringLiteral("Failed to start build_command."));
        return;
    }
    m_outputPanel->appendLine(QStringLiteral("$ ") + substituted);
    m_compilePollTimer->start(100); /* same non-blocking-poll shape as ase_lsp_client_poll */
}

void EditorViewport::pollCompile() {
    if (m_compileProcess == nullptr) {
        m_compilePollTimer->stop();
        return;
    }

    char buf[4096];
    for (;;) {
        long n = ase_process_read(m_compileProcess, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        m_outputPanel->appendText(QString::fromUtf8(buf, static_cast<int>(n)));
    }

    if (ase_process_has_exited(m_compileProcess)) {
        m_outputPanel->appendLine(QStringLiteral("[exit code %1]").arg(ase_process_exit_code(m_compileProcess)));
        ase_process_destroy(m_compileProcess);
        m_compileProcess = nullptr;
        m_compilePollTimer->stop();
    }
}

/* Shared by undo()/redo(): apply the cursor snapshot the undo stack
 * handed back, refresh everything downstream of a buffer mutation, and
 * render instantly (like any other edit — see snapAnimationToTarget's
 * doc comment) rather than gliding. */
void EditorViewport::applyUndoResult(size_t *cursors, size_t count) {
    /* The undo stack only snapshots point offsets (docs/adr/0018), so
     * restoring here always lands with no active selection — matches
     * how typing over a selection collapses it too. */
    m_cursors.clear();
    m_selectionAnchors.clear();
    m_cursors.reserve(static_cast<int>(count));
    m_selectionAnchors.reserve(static_cast<int>(count));
    for (size_t i = 0; i < count; ++i) {
        m_cursors.push_back(cursors[i]);
        m_selectionAnchors.push_back(cursors[i]);
    }
    free(cursors);
    if (m_cursors.isEmpty()) {
        m_cursors.push_back(0);
        m_selectionAnchors.push_back(0);
    }

    /* Any undo/redo marks dirty — simpler than tracking the exact saved
     * stack position, an acceptable v1 gap (docs/adr/0023) since the
     * common case (undo back to a saved state, still see the dirty
     * marker) is a minor cosmetic paper cut, not a data-loss risk. */
    m_dirty = true;
    refreshCache();
    ensureCursorVisible();
    resetCaretBlink();
    snapAnimationToTarget();
    update();
}

void EditorViewport::undo() {
    size_t *cursors = nullptr;
    size_t count = 0;
    if (!ase_undo_undo(m_undo, m_buffer, &cursors, &count)) {
        return;
    }
    applyUndoResult(cursors, count);
}

void EditorViewport::redo() {
    size_t *cursors = nullptr;
    size_t count = 0;
    if (!ase_undo_redo(m_undo, m_buffer, &cursors, &count)) {
        return;
    }
    applyUndoResult(cursors, count);
}
