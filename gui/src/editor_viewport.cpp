#include "editor_viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

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
    int line = m_scrollLine + pos.y() / std::max(1, m_lineHeight);
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int col = pos.x() / std::max(1, m_charWidth);
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
    QPainter painter(this);
    painter.fillRect(rect(), m_backgroundColor);

    int visibleLines = std::max(1, height() / m_lineHeight + 1);
    int firstLine = m_scrollLine;
    int lastLine = std::min(firstLine + visibleLines, static_cast<int>(m_lineStarts.size()));

    for (int line = firstLine; line < lastLine; ++line) {
        int start = m_lineStarts[line];
        int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        int y = (line - firstLine) * m_lineHeight;
        drawLine(painter, start, end, y);
    }

    int caretAlpha = 255;
    if (m_animationsEnabled) {
        double phase = (m_caretTick % kCaretAnimationTicks) / static_cast<double>(kCaretAnimationTicks);
        caretAlpha = std::clamp(static_cast<int>(128 + 127 * std::sin(phase * kTwoPi)), 0, 255);
    } else if (!m_caretVisible) {
        caretAlpha = 0;
    }

    if (caretAlpha > 0) {
        QColor caretColor = m_textColor;
        caretColor.setAlpha(caretAlpha);
        for (size_t cursor : m_cursors) {
            int line = lineForOffset(cursor);
            if (line >= firstLine && line < lastLine) {
                int col = columnForOffset(cursor, line);
                int x = col * m_charWidth;
                int y = (line - firstLine) * m_lineHeight;
                painter.fillRect(QRect(x, y, 2, m_lineHeight), caretColor);
            }
        }
    }
}

/* Splits [start, end) into same-capture runs and draws each with its own
 * style. All captures render in m_textColor's hue — only opacity/weight/style
 * vary — so the query's captures don't need to be mutually exclusive in
 * general, just non-overlapping in practice for the leaf-level nodes
 * c_highlights.scm captures. See docs/adr/0007, decision 1. */
void EditorViewport::drawLine(QPainter &painter, int start, int end, int y) {
    int lineLen = end - start;
    if (lineLen <= 0) {
        return;
    }

    std::vector<AseHighlightCapture> captures(static_cast<size_t>(lineLen), ASE_HL_NONE);
    for (const AseHighlightSpan &span : m_highlights) {
        int spanStart = static_cast<int>(span.start);
        int spanEnd = static_cast<int>(span.end);
        if (spanEnd <= start || spanStart >= end) {
            continue;
        }
        int clampedStart = std::max(spanStart, start);
        int clampedEnd = std::min(spanEnd, end);
        for (int i = clampedStart; i < clampedEnd; ++i) {
            captures[static_cast<size_t>(i - start)] = span.capture;
        }
    }

    int x = 0;
    int runStart = 0;
    for (int i = 1; i <= lineLen; ++i) {
        if (i < lineLen && captures[static_cast<size_t>(i)] == captures[static_cast<size_t>(runStart)]) {
            continue;
        }
        int runLen = i - runStart;
        QString text = QString::fromUtf8(m_cache.constData() + start + runStart, runLen);
        applyCaptureStyle(painter, captures[static_cast<size_t>(runStart)]);
        painter.drawText(QRect(x, y, width() - x, m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter, text);
        x += runLen * m_charWidth;
        runStart = i;
    }
}

void EditorViewport::applyCaptureStyle(QPainter &painter, AseHighlightCapture capture) {
    QFont font = m_font;
    QColor color = m_textColor;

    switch (capture) {
    case ASE_HL_KEYWORD:
        font.setBold(true);
        break;
    case ASE_HL_TYPE:
        font.setItalic(true);
        break;
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
    case ASE_HL_NONE:
    default:
        break;
    }

    painter.setFont(font);
    painter.setPen(color);
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
    int line = lineForOffset(m_cursors.last());
    int visibleLines = std::max(1, height() / m_lineHeight);
    if (line < m_scrollLine) {
        m_scrollLine = line;
    } else if (line >= m_scrollLine + visibleLines) {
        m_scrollLine = line - visibleLines + 1;
    }
}

void EditorViewport::save() {
    if (m_filePath.isEmpty()) {
        return;
    }
    ase_buffer_save_to_file(m_buffer, m_filePath.toUtf8().constData());
}
