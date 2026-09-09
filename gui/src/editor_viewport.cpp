#include "editor_viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

namespace {
constexpr int kCaretAnimationTicks = 34; /* ~1020ms period at the 30ms tick below */
constexpr double kTwoPi = 6.283185307179586;
constexpr int kCaretWidth = 2;
constexpr int kGutterPadding = 8; /* on each side of the line-number text */
constexpr double kEaseFactor = 0.35; /* per paint — see docs/adr/0015 */

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

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_caretTick++;
        if (m_animationsEnabled) {
            update();
        } else if (m_caretTick % 17 == 0) { /* ~500ms at this 30ms tick */
            m_caretVisible = !m_caretVisible;
            update();
        }
    });
    m_blinkTimer->start(30);

    m_configTimer = new QTimer(this);
    connect(m_configTimer, &QTimer::timeout, this, [this]() { checkConfigReload(); });
    m_configTimer->start(750); /* see docs/adr/0008, decision 4 */
}

EditorViewport::~EditorViewport() {
    ase_syntax_destroy(m_syntax);
    ase_config_destroy(m_config);
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

    const char *familyStr = ase_config_get_string(m_config, "font_family");
    QString family = familyStr != nullptr ? QString::fromUtf8(familyStr) : QStringLiteral("monospace");
    long size = ase_config_get_int(m_config, "font_size", 12);

    m_font = family.compare(QLatin1String("monospace"), Qt::CaseInsensitive) == 0
                 ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                 : QFont(family);
    m_font.setPointSize(static_cast<int>(size));

    QFontMetrics metrics(m_font);
    m_lineHeight = metrics.height();
    m_charWidth = metrics.horizontalAdvance(QLatin1Char('M'));

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
    int col = localX / std::max(1, m_charWidth);
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
    int caretAlpha = 255;
    if (m_animationsEnabled) {
        double phase = (m_caretTick % kCaretAnimationTicks) / static_cast<double>(kCaretAnimationTicks);
        caretAlpha = std::clamp(static_cast<int>(128 + 127 * std::sin(phase * kTwoPi)), 0, 255);
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
        m_renderedScrollLine = m_scrollLine;
        m_renderedScrollX = m_scrollX;
        m_renderedCaretPos.clear();
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
 * for why the latter drifts visibly on longer lines. */
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
        QFont runFont = fontForCapture(captures[runStart]);
        painter.setFont(runFont);
        painter.setPen(colorForCapture(captures[runStart]));
        painter.drawText(QRect(x, y, width() - x, m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter, text);
        x += QFontMetrics(runFont).horizontalAdvance(text);
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
        QFont runFont = fontForCapture(captures[runStart]);
        x += QFontMetrics(runFont).horizontalAdvance(text);
        runStart = i;
    }
    return x;
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
    m_caretVisible = true;

    if (event->key() == Qt::Key_Up) {
        moveCursorVertically(-1);
        ensureCursorVisible();
        update();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        moveCursorVertically(1);
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

    switch (event->key()) {
    case Qt::Key_Left:
        moveCursorLeft();
        break;
    case Qt::Key_Right:
        moveCursorRight();
        break;
    case Qt::Key_Home:
        moveCursorHome();
        break;
    case Qt::Key_End:
        moveCursorEnd();
        break;
    case Qt::Key_Backspace:
        deleteBackward();
        break;
    case Qt::Key_Delete:
        deleteForward();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        insertText(QByteArrayLiteral("\n"));
        break;
    default:
        if (event->modifiers() & Qt::ControlModifier) {
            if (event->key() == Qt::Key_S) {
                save();
                return;
            }
            if (event->key() == Qt::Key_Q) {
                window()->close();
                return;
            }
            if (event->key() == Qt::Key_D) {
                addCursorAtNextOccurrence();
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
        }
    }

    ensureCursorVisible();
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

    if (event->modifiers() & Qt::AltModifier) {
        m_cursors.push_back(offset);
        normalizeCursors();
    } else {
        m_cursors.clear();
        m_cursors.push_back(offset);
    }

    m_desiredColumn = -1;
    m_caretVisible = true;
    ensureCursorVisible();
    update();
}

void EditorViewport::normalizeCursors() {
    std::sort(m_cursors.begin(), m_cursors.end());
    m_cursors.erase(std::unique(m_cursors.begin(), m_cursors.end()), m_cursors.end());
    if (m_cursors.isEmpty()) {
        m_cursors.push_back(0);
    }
}

void EditorViewport::collapseToOneCursor() {
    if (m_cursors.size() <= 1) {
        return;
    }
    size_t keep = m_cursors.last();
    m_cursors.clear();
    m_cursors.push_back(keep);
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
            m_cursors.push_back(static_cast<size_t>(searchStart + wordLen));
            normalizeCursors();
            ensureCursorVisible();
            update();
            return;
        }
    }
    /* no further occurrence forward — no-op, see docs/adr/0012 */
}

void EditorViewport::insertText(const QByteArray &bytes) {
    if (bytes.isEmpty()) {
        return;
    }
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        insertTextAt(m_cursors[i], bytes);
    }
    normalizeCursors();
    refreshCache();
}

void EditorViewport::insertTextAt(size_t &cursor, const QByteArray &bytes) {
    if (ase_buffer_insert(m_buffer, cursor, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        cursor += static_cast<size_t>(bytes.size());
    }
}

void EditorViewport::deleteBackward() {
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        deleteBackwardAt(m_cursors[i]);
    }
    normalizeCursors();
    refreshCache();
}

void EditorViewport::deleteBackwardAt(size_t &cursor) {
    if (cursor == 0) {
        return;
    }
    size_t start = cursor - 1;
    while (start > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(start)])) {
        start--;
    }
    if (ase_buffer_delete(m_buffer, start, cursor - start)) {
        cursor = start;
    }
}

void EditorViewport::deleteForward() {
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        deleteForwardAt(m_cursors[i]);
    }
    normalizeCursors();
    refreshCache();
}

void EditorViewport::deleteForwardAt(size_t &cursor) {
    size_t len = static_cast<size_t>(m_cache.size());
    if (cursor >= len) {
        return;
    }
    size_t end = cursor + 1;
    while (end < len && isUtf8ContinuationByte(m_cache[static_cast<int>(end)])) {
        end++;
    }
    ase_buffer_delete(m_buffer, cursor, end - cursor);
}

void EditorViewport::moveCursorLeft() {
    for (size_t &cursor : m_cursors) {
        moveCursorLeftAt(cursor);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorLeftAt(size_t &cursor) {
    if (cursor == 0) {
        return;
    }
    size_t pos = cursor - 1;
    while (pos > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
        pos--;
    }
    cursor = pos;
}

void EditorViewport::moveCursorRight() {
    for (size_t &cursor : m_cursors) {
        moveCursorRightAt(cursor);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorRightAt(size_t &cursor) {
    size_t len = static_cast<size_t>(m_cache.size());
    if (cursor >= len) {
        return;
    }
    size_t pos = cursor + 1;
    while (pos < len && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
        pos++;
    }
    cursor = pos;
}

void EditorViewport::moveCursorVertically(int lineDelta) {
    if (m_cursors.size() == 1) {
        /* sticky column — see docs/adr/0012, decision 1 */
        size_t cursor = m_cursors[0];
        int line = lineForOffset(cursor);
        int column = (m_desiredColumn >= 0) ? m_desiredColumn : columnForOffset(cursor, line);
        m_desiredColumn = column;

        int newLine = line + lineDelta;
        if (newLine >= 0 && newLine < m_lineStarts.size()) {
            m_cursors[0] = offsetForLineColumn(newLine, column);
        }
        return;
    }

    for (size_t &cursor : m_cursors) {
        moveCursorVerticallyAt(cursor, lineDelta);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorVerticallyAt(size_t &cursor, int lineDelta) {
    int line = lineForOffset(cursor);
    int column = columnForOffset(cursor, line);
    int newLine = line + lineDelta;
    if (newLine < 0 || newLine >= m_lineStarts.size()) {
        return;
    }
    cursor = offsetForLineColumn(newLine, column);
}

void EditorViewport::moveCursorHome() {
    for (size_t &cursor : m_cursors) {
        moveCursorHomeAt(cursor);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorHomeAt(size_t &cursor) {
    int line = lineForOffset(cursor);
    cursor = static_cast<size_t>(m_lineStarts[line]);
}

void EditorViewport::moveCursorEnd() {
    for (size_t &cursor : m_cursors) {
        moveCursorEndAt(cursor);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorEndAt(size_t &cursor) {
    int line = lineForOffset(cursor);
    int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    cursor = static_cast<size_t>(end);
}

void EditorViewport::ensureCursorVisible() {
    size_t cursor = m_cursors.last();
    int line = lineForOffset(cursor);
    int visibleLines = std::max(1, height() / m_lineHeight);
    if (line < m_scrollLine) {
        m_scrollLine = line;
    } else if (line >= m_scrollLine + visibleLines) {
        m_scrollLine = line - visibleLines + 1;
    }

    /* Horizontal half, symmetric to the vertical logic above — see
     * docs/adr/0014, decision 3. */
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForOffset(cursor, line);
    int caretX = xForColumn(lineStart, lineEnd, col);

    int textAreaWidth = std::max(1, width() - gutterWidth());
    if (caretX < m_scrollX) {
        m_scrollX = caretX;
    } else if (caretX + kCaretWidth > m_scrollX + textAreaWidth) {
        m_scrollX = caretX + kCaretWidth - textAreaWidth;
    }
    m_scrollX = std::max(0, m_scrollX);
}

void EditorViewport::save() {
    if (m_filePath.isEmpty()) {
        return;
    }
    ase_buffer_save_to_file(m_buffer, m_filePath.toUtf8().constData());
}
