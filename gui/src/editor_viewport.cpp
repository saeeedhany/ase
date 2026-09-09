#include "editor_viewport.h"

#include <algorithm>

#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

namespace {
constexpr QColor kBackground(0x28, 0x28, 0x28);
constexpr QColor kText(0xF5, 0xE6, 0xC8);

bool isUtf8ContinuationByte(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}
} // namespace

EditorViewport::EditorViewport(AseBuffer *buffer, QString filePath, QWidget *parent)
    : QWidget(parent), m_buffer(buffer), m_filePath(std::move(filePath)) {
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::NoContextMenu);
    setAutoFillBackground(false);

    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font.setPointSize(12);

    QFontMetrics metrics(m_font);
    m_lineHeight = metrics.height();
    m_charWidth = metrics.horizontalAdvance(QLatin1Char('M'));

    refreshCache();

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_caretVisible = !m_caretVisible;
        update();
    });
    m_blinkTimer->start(500);
}

EditorViewport::~EditorViewport() {
    ase_buffer_destroy(m_buffer);
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

void EditorViewport::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), kBackground);
    painter.setFont(m_font);
    painter.setPen(kText);

    int visibleLines = std::max(1, height() / m_lineHeight + 1);
    int firstLine = m_scrollLine;
    int lastLine = std::min(firstLine + visibleLines, static_cast<int>(m_lineStarts.size()));

    for (int line = firstLine; line < lastLine; ++line) {
        int start = m_lineStarts[line];
        int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        QString text = QString::fromUtf8(m_cache.constData() + start, end - start);
        int y = (line - firstLine) * m_lineHeight;
        painter.drawText(QRect(0, y, width(), m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter, text);
    }

    if (m_caretVisible) {
        int line = lineForOffset(m_cursor);
        if (line >= firstLine && line < lastLine) {
            int col = columnForOffset(m_cursor, line);
            int x = col * m_charWidth;
            int y = (line - firstLine) * m_lineHeight;
            painter.fillRect(QRect(x, y, 2, m_lineHeight), kText);
        }
    }
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

void EditorViewport::insertText(const QByteArray &bytes) {
    if (bytes.isEmpty()) {
        return;
    }
    if (!ase_buffer_insert(m_buffer, m_cursor, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        return;
    }
    m_cursor += static_cast<size_t>(bytes.size());
    refreshCache();
}

void EditorViewport::deleteBackward() {
    if (m_cursor == 0) {
        return;
    }
    size_t start = m_cursor - 1;
    while (start > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(start)])) {
        start--;
    }
    if (ase_buffer_delete(m_buffer, start, m_cursor - start)) {
        m_cursor = start;
        refreshCache();
    }
}

void EditorViewport::deleteForward() {
    size_t len = static_cast<size_t>(m_cache.size());
    if (m_cursor >= len) {
        return;
    }
    size_t end = m_cursor + 1;
    while (end < len && isUtf8ContinuationByte(m_cache[static_cast<int>(end)])) {
        end++;
    }
    if (ase_buffer_delete(m_buffer, m_cursor, end - m_cursor)) {
        refreshCache();
    }
}

void EditorViewport::moveCursorLeft() {
    if (m_cursor == 0) {
        return;
    }
    size_t pos = m_cursor - 1;
    while (pos > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
        pos--;
    }
    m_cursor = pos;
}

void EditorViewport::moveCursorRight() {
    size_t len = static_cast<size_t>(m_cache.size());
    if (m_cursor >= len) {
        return;
    }
    size_t pos = m_cursor + 1;
    while (pos < len && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
        pos++;
    }
    m_cursor = pos;
}

void EditorViewport::moveCursorVertically(int lineDelta) {
    int line = lineForOffset(m_cursor);
    int column = (m_desiredColumn >= 0) ? m_desiredColumn : columnForOffset(m_cursor, line);
    m_desiredColumn = column;

    int newLine = line + lineDelta;
    if (newLine < 0 || newLine >= m_lineStarts.size()) {
        return;
    }
    m_cursor = offsetForLineColumn(newLine, column);
}

void EditorViewport::moveCursorHome() {
    int line = lineForOffset(m_cursor);
    m_cursor = static_cast<size_t>(m_lineStarts[line]);
}

void EditorViewport::moveCursorEnd() {
    int line = lineForOffset(m_cursor);
    int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    m_cursor = static_cast<size_t>(end);
}

void EditorViewport::ensureCursorVisible() {
    int line = lineForOffset(m_cursor);
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
