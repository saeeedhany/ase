#include "editor_viewport.h"

#include "editor_viewport_internal.h"

#include "command_line.h"

#include <algorithm>
#include <utility>

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>

/* ------------------------------------------------------------- Vim mode */
/* Phase 1 — see docs/adr/0046. Everything below operates on m_cursors[0]
 * only; handleVimNormalOrVisualKey() collapses to one cursor the moment
 * any Vim key is pressed. */

/* Anchor and cursor are pushed out to the *outer* edges of the two lines
 * involved, in whichever order they currently sit, so d/y/c and the
 * selection highlight all see exactly the whole lines — none of them
 * need to know linewise Visual exists. See docs/adr/0056. */
void EditorViewport::vimPrepareLinewiseMotion() {
    if (!m_vimVisualLinewise || m_vimMode != VimMode::Visual) {
        return;
    }
    int line = std::clamp(m_vimVisualCursorLine, 0, static_cast<int>(m_lineStarts.size()) - 1);
    m_cursors[0] = static_cast<size_t>(m_lineStarts[line]);
}

void EditorViewport::vimNormalizeLinewiseSelection() {
    if (!m_vimVisualLinewise || m_vimMode != VimMode::Visual) {
        return;
    }
    int lineCount = static_cast<int>(m_lineStarts.size());
    int anchorLine = std::clamp(m_vimVisualAnchorLine, 0, lineCount - 1);
    int cursorLine = std::clamp(lineForOffset(m_cursors[0]), 0, lineCount - 1);
    m_vimVisualCursorLine = cursorLine;

    auto lineStart = [this](int line) { return static_cast<size_t>(m_lineStarts[line]); };
    auto lineEnd = [this, lineCount](int line) {
        return (line + 1 < lineCount) ? static_cast<size_t>(m_lineStarts[line + 1])
                                      : static_cast<size_t>(m_cache.size());
    };

    if (cursorLine >= anchorLine) {
        m_selectionAnchors[0] = lineStart(anchorLine);
        m_cursors[0] = lineEnd(cursorLine);
    } else {
        m_selectionAnchors[0] = lineEnd(anchorLine);
        m_cursors[0] = lineStart(cursorLine);
    }
}

void EditorViewport::resetVimPendingState() {
    m_vimCount1 = 0;
    m_vimCount2 = 0;
    m_vimPendingOperator = '\0';
    m_vimPendingG = false;
}

/* '\n' counts as Blank, not its own class — the key trick that makes
 * w/b/e cross line boundaries with zero special-casing, matching real
 * vim's "a blank line acts like whitespace" model. */
EditorViewport::VimCharClass EditorViewport::vimClassifyAt(size_t pos) const {
    char c = m_cache[static_cast<int>(pos)];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        return VimCharClass::Blank;
    }
    if (isWordChar(c)) {
        return VimCharClass::Word;
    }
    return VimCharClass::Punct;
}

size_t EditorViewport::vimNextCharBoundary(size_t pos) const {
    size_t len = static_cast<size_t>(m_cache.size());
    if (pos >= len) {
        return len;
    }
    pos++;
    while (pos < len && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
        pos++;
    }
    return pos;
}

size_t EditorViewport::vimPrevCharBoundary(size_t pos) const {
    if (pos == 0) {
        return 0;
    }
    pos--;
    while (pos > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
        pos--;
    }
    return pos;
}

size_t EditorViewport::vimWordForward(size_t pos) const {
    size_t len = static_cast<size_t>(m_cache.size());
    if (pos >= len) {
        return len;
    }
    VimCharClass start = vimClassifyAt(pos);
    if (start != VimCharClass::Blank) {
        while (pos < len && vimClassifyAt(pos) == start) {
            pos = vimNextCharBoundary(pos);
        }
    }
    while (pos < len && vimClassifyAt(pos) == VimCharClass::Blank) {
        pos = vimNextCharBoundary(pos);
    }
    return pos;
}

size_t EditorViewport::vimWordEnd(size_t pos) const {
    size_t len = static_cast<size_t>(m_cache.size());
    if (pos >= len) {
        return pos;
    }
    pos = vimNextCharBoundary(pos);
    while (pos < len && vimClassifyAt(pos) == VimCharClass::Blank) {
        pos = vimNextCharBoundary(pos);
    }
    if (pos >= len) {
        return len;
    }
    VimCharClass cls = vimClassifyAt(pos);
    size_t next = vimNextCharBoundary(pos);
    while (next < len && vimClassifyAt(next) == cls) {
        pos = next;
        next = vimNextCharBoundary(next);
    }
    return pos;
}

size_t EditorViewport::vimWordBackward(size_t pos) const {
    if (pos == 0) {
        return 0;
    }
    pos = vimPrevCharBoundary(pos);
    while (pos > 0 && vimClassifyAt(pos) == VimCharClass::Blank) {
        pos = vimPrevCharBoundary(pos);
    }
    if (vimClassifyAt(pos) == VimCharClass::Blank) {
        return 0;
    }
    VimCharClass cls = vimClassifyAt(pos);
    while (pos > 0) {
        size_t prev = vimPrevCharBoundary(pos);
        if (vimClassifyAt(prev) != cls) {
            break;
        }
        pos = prev;
    }
    return pos;
}

size_t EditorViewport::vimFirstNonBlank(int line) const {
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int start = m_lineStarts[line];
    int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int pos = start;
    while (pos < end && (m_cache[pos] == ' ' || m_cache[pos] == '\t')) {
        pos++;
    }
    return static_cast<size_t>(pos);
}

void EditorViewport::vimGotoLine(int line) {
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    if (m_vimPendingOperator != '\0') {
        int beforeLine = lineForOffset(m_cursors[0]);
        int startLine = std::min(beforeLine, line);
        int lineCount = std::abs(line - beforeLine) + 1;
        vimApplyPendingOperatorLinewise(startLine, lineCount);
        return;
    }
    size_t target = vimFirstNonBlank(line);
    m_cursors[0] = target;
    if (m_vimMode != VimMode::Visual) {
        m_selectionAnchors[0] = target;
    }
}

void EditorViewport::vimExecuteMotion(char m, int count) {
    bool visual = (m_vimMode == VimMode::Visual);
    size_t before = m_cursors[0];

    if (m_vimPendingOperator == '\0') {
        for (int n = 0; n < count; ++n) {
            switch (m) {
            case 'h':
                moveCursorLeftAt(0, visual);
                break;
            case 'l':
                moveCursorRightAt(0, visual);
                break;
            case 'j':
                moveCursorVerticallyAt(0, 1, visual);
                break;
            case 'k':
                moveCursorVerticallyAt(0, -1, visual);
                break;
            case '0':
                moveCursorHomeAt(0, visual);
                return;
            case '^': {
                size_t target = vimFirstNonBlank(lineForOffset(m_cursors[0]));
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                return;
            }
            case '$':
                moveCursorEndAt(0, visual);
                return;
            case 'w': {
                size_t target = vimWordForward(m_cursors[0]);
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                break;
            }
            case 'b': {
                size_t target = vimWordBackward(m_cursors[0]);
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                break;
            }
            case 'e': {
                size_t target = vimWordEnd(m_cursors[0]);
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                break;
            }
            default:
                return;
            }
        }
        return;
    }

    /* Operator pending — j/k span whole lines; everything else is an
     * exact byte range from `before` to wherever `count` applications
     * of the motion land. */
    if (m == 'j' || m == 'k') {
        int beforeLine = lineForOffset(before);
        int deltaLines = (m == 'j') ? count : -count;
        int targetLine = std::clamp(beforeLine + deltaLines, 0, static_cast<int>(m_lineStarts.size()) - 1);
        int startLine = std::min(beforeLine, targetLine);
        int lineCount = std::abs(targetLine - beforeLine) + 1;
        vimApplyPendingOperatorLinewise(startLine, lineCount);
        return;
    }

    size_t after = before;
    if (m == '0') {
        after = static_cast<size_t>(m_lineStarts[lineForOffset(before)]);
    } else if (m == '^') {
        after = vimFirstNonBlank(lineForOffset(before));
    } else if (m == '$') {
        int line = lineForOffset(before);
        after = (line + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[line + 1] - 1)
                                                  : static_cast<size_t>(m_cache.size());
    } else {
        for (int n = 0; n < count; ++n) {
            switch (m) {
            case 'h':
                after = vimPrevCharBoundary(after);
                break;
            case 'l':
                after = vimNextCharBoundary(after);
                break;
            case 'w':
                after = vimWordForward(after);
                break;
            case 'b':
                after = vimWordBackward(after);
                break;
            case 'e':
                /* Inclusive of the char the motion itself lands on —
                 * "d/y/c to the end of this word" removes that last
                 * character too, unlike a plain 'e' cursor move. */
                after = vimNextCharBoundary(vimWordEnd(after));
                break;
            default:
                break;
            }
        }
    }
    size_t start = std::min(before, after);
    size_t end = std::max(before, after);
    vimApplyPendingOperatorCharwise(start, end);
}

void EditorViewport::vimApplyPendingOperatorCharwise(size_t start, size_t end) {
    char op = m_vimPendingOperator;
    resetVimPendingState();
    if (start >= end) {
        return;
    }
    switch (op) {
    case 'd':
        vimDeleteRange(start, end);
        break;
    case 'y':
        vimYankRange(start, end, false);
        break;
    case 'c':
        vimChangeRange(start, end);
        break;
    default:
        break;
    }
}

void EditorViewport::vimApplyPendingOperatorLinewise(int startLine, int lineCount) {
    char op = m_vimPendingOperator;
    resetVimPendingState();
    switch (op) {
    case 'd':
        vimDeleteLines(startLine, lineCount);
        break;
    case 'y':
        vimYankLines(startLine, lineCount);
        break;
    case 'c':
        /* Real vim leaves one blank line and enters insert there; v1
         * simplification (documented, docs/adr/0046) — just delete the
         * lines and enter insert at the resulting cursor position. */
        vimDeleteLines(startLine, lineCount);
        m_vimMode = VimMode::Insert;
        break;
    default:
        break;
    }
}

void EditorViewport::vimDeleteRange(size_t start, size_t end) {
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    ensureCursorVisible();
    update();
}

void EditorViewport::vimYankRange(size_t start, size_t end, bool linewise) {
    QByteArray text = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    QGuiApplication::clipboard()->setText(QString::fromUtf8(text));
    m_vimLastYankWasLinewise = linewise;
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimChangeRange(size_t start, size_t end) {
    vimDeleteRange(start, end);
    m_vimMode = VimMode::Insert;
}

void EditorViewport::vimDeleteLines(int startLine, int count) {
    startLine = std::clamp(startLine, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int endLine = std::clamp(startLine + count - 1, startLine, static_cast<int>(m_lineStarts.size()) - 1);
    size_t start = static_cast<size_t>(m_lineStarts[startLine]);
    /* Consume through the start of the line *after* endLine so the
     * trailing newline goes with it too (leaves no blank line behind);
     * on the buffer's last line, there's no following '\n' to eat. */
    size_t end = (endLine + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[endLine + 1])
                                                      : static_cast<size_t>(m_cache.size());
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    int newLine = std::clamp(startLine, 0, static_cast<int>(m_lineStarts.size()) - 1);
    size_t target = vimFirstNonBlank(newLine);
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimYankLines(int startLine, int count) {
    startLine = std::clamp(startLine, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int endLine = std::clamp(startLine + count - 1, startLine, static_cast<int>(m_lineStarts.size()) - 1);
    size_t start = static_cast<size_t>(m_lineStarts[startLine]);
    size_t end = (endLine + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[endLine + 1])
                                                      : static_cast<size_t>(m_cache.size());
    QByteArray text = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    QGuiApplication::clipboard()->setText(QString::fromUtf8(text));
    m_vimLastYankWasLinewise = true;
    size_t target = vimFirstNonBlank(startLine);
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimPasteAfter() {
    QString clip = QGuiApplication::clipboard()->text();
    if (clip.isEmpty()) {
        return;
    }
    QByteArray bytes = clip.toUtf8();
    size_t insertAt;
    if (m_vimLastYankWasLinewise) {
        int line = lineForOffset(m_cursors[0]);
        insertAt = (line + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[line + 1])
                                                     : static_cast<size_t>(m_cache.size());
        if (insertAt == static_cast<size_t>(m_cache.size()) && !bytes.endsWith('\n')) {
            bytes.append('\n');
        }
    } else {
        /* At end-of-line, this editor's own cursor convention already
         * sits *at* the '\n' byte (an insertion gap, not "on" a real
         * character — a named fidelity gap vs. real vim, see
         * docs/adr/0046). vimNextCharBoundary assumes its argument
         * points at a real character to step past; blindly applying it
         * here would step over the '\n' itself and land the paste at
         * the start of the *next* line instead of appending it to the
         * end of the current one. */
        size_t cur = m_cursors[0];
        bool atNewline = cur < static_cast<size_t>(m_cache.size()) && m_cache[static_cast<int>(cur)] == '\n';
        insertAt = atNewline ? cur : vimNextCharBoundary(cur);
    }
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_insert(m_buffer, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()));
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    size_t target = m_vimLastYankWasLinewise ? vimFirstNonBlank(lineForOffset(insertAt)) : insertAt;
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimPasteBefore() {
    QString clip = QGuiApplication::clipboard()->text();
    if (clip.isEmpty()) {
        return;
    }
    QByteArray bytes = clip.toUtf8();
    size_t insertAt = m_vimLastYankWasLinewise ? static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])
                                                : m_cursors[0];
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_insert(m_buffer, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()));
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    size_t target = m_vimLastYankWasLinewise ? vimFirstNonBlank(lineForOffset(insertAt)) : insertAt;
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimOpenLineAbove() {
    int line = lineForOffset(m_cursors[0]);
    size_t at = static_cast<size_t>(m_lineStarts[line]);
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_insert(m_buffer, at, "\n", 1)) {
        ase_undo_record_insert(m_undo, at, "\n", 1);
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_cursors[0] = at;
    m_selectionAnchors[0] = at;
    m_dirty = true;
    refreshCache();
    ensureCursorVisible();
    update();
}

QString EditorViewport::vimBlockGlyphAt(size_t cursor, int *width, AseHighlightCapture *capture) const {
    int line = lineForOffset(cursor);
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForOffset(cursor, line);
    /* vimNextCharBoundary steps by codepoint, not by byte, so a
     * multi-byte UTF-8 character under the cursor is measured/drawn
     * whole rather than split mid-sequence. At end of line/buffer it
     * returns `cursor` unchanged (nothing to step past), which is
     * exactly the "no real character here" signal below. */
    size_t glyphEnd = vimNextCharBoundary(cursor);
    int endCol = std::min(static_cast<int>(glyphEnd) - lineStart, lineEnd - lineStart);
    if (endCol <= col) {
        *width = m_charWidth;
        *capture = ASE_HL_NONE;
        return QString();
    }

    QVector<AseHighlightCapture> captures = capturesForLine(lineStart, lineEnd);
    *capture = captures[col];
    *width = xForColumn(lineStart, lineEnd, endCol) - xForColumn(lineStart, lineEnd, col);
    return QString::fromUtf8(m_cache.constData() + lineStart + col, endCol - col);
}

/* Top-level Normal/Visual key dispatcher — see docs/adr/0046 for the
 * full state-machine table. Returns true for every key Vim claims,
 * including "recognized but currently invalid, swallowed" cases;
 * false only for keys outside Vim's alphabet entirely (arrows, Home/
 * End, every Ctrl/Alt/Meta combo, function keys), which the caller's
 * own switch/Ctrl-chain still handles unmodified. */
bool EditorViewport::handleVimNormalOrVisualKey(QKeyEvent *event) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    if (m_cursors.size() > 1) {
        collapseToOneCursor();
    }

    if (event->key() == Qt::Key_Backspace) {
        vimExecuteMotion('h', std::max(1, m_vimCount1) * std::max(1, m_vimCount2));
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    QString text = event->text();
    if (text.isEmpty()) {
        return false;
    }
    QChar qc = text.at(0);

    /* Mid-"gg": resolved on the very next key, whatever it is. */
    if (m_vimPendingG) {
        m_vimPendingG = false;
        if (qc == QLatin1Char('g')) {
            int targetLine = (m_vimCount1 > 0) ? (m_vimCount1 - 1) : 0;
            vimPrepareLinewiseMotion();
            vimGotoLine(targetLine);
        }
        vimNormalizeLinewiseSelection();
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    /* Counts. A bare '0' (no count typed yet) is the "column 0" motion,
     * not a digit — matches real vim. */
    if (qc.isDigit()) {
        int d = qc.digitValue();
        int &count = (m_vimPendingOperator != '\0') ? m_vimCount2 : m_vimCount1;
        if (!(d == 0 && count == 0)) {
            count = count * 10 + d;
            return true;
        }
    }

    char c = qc.toLatin1(); /* '\0' for non-Latin1 — falls through to "unrecognized" below. */
    int count = std::max(1, m_vimCount1) * std::max(1, m_vimCount2);

    if (c == ':') {
        /* Real vim's own ex-command-line trigger — bare `:` (Shift+;
         * on most layouts), not the app-wide Ctrl+; (which still works
         * everywhere, Vim mode or not — see docs/adr/0025). Only live
         * here, in Normal/Visual dispatch: Insert mode still needs `:`
         * to type as a literal character. */
        if (m_commandLine != nullptr) {
            m_commandLine->openCommandLine();
        }
        resetVimPendingState();
        return true;
    }
    if (c == 'g') {
        m_vimPendingG = true;
        return true;
    }
    if (c == 'G') {
        int targetLine = (m_vimCount1 > 0) ? (m_vimCount1 - 1) : static_cast<int>(m_lineStarts.size()) - 1;
        vimPrepareLinewiseMotion();
        vimGotoLine(targetLine);
        vimNormalizeLinewiseSelection();
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }
    if (c == 'h' || c == 'l' || c == 'j' || c == 'k' || c == '0' || c == '^' || c == '$' || c == 'w' ||
        c == 'b' || c == 'e') {
        /* A pure motion (no operator resolved here) glides like every
         * other navigation in this app — arrows, Home/End — rather than
         * snapping. See docs/adr/0051. */
        vimPrepareLinewiseMotion();
        vimExecuteMotion(c, count);
        vimNormalizeLinewiseSelection();
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    if (m_vimMode == VimMode::Visual) {
        switch (c) {
        case 'v':
            /* v inside linewise Visual drops back to charwise rather
             * than leaving Visual — matches real vim, where the two
             * Visual flavours toggle between each other. */
            if (m_vimVisualLinewise) {
                m_vimVisualLinewise = false;
            } else {
                collapseToOneCursor();
                m_vimMode = VimMode::Normal;
            }
            break;
        case 'V':
            if (m_vimVisualLinewise) {
                collapseToOneCursor();
                m_vimMode = VimMode::Normal;
                m_vimVisualLinewise = false;
            } else {
                m_vimVisualLinewise = true;
                m_vimVisualAnchorLine = lineForOffset(m_selectionAnchors[0]);
                m_vimVisualCursorLine = lineForOffset(m_cursors[0]);
                vimNormalizeLinewiseSelection();
            }
            break;
        case 'o': {
            /* Jump to the other end of the selection, keeping it — lets
             * you fix the end you didn't mean to extend without
             * reselecting from scratch. */
            std::swap(m_cursors[0], m_selectionAnchors[0]);
            if (m_vimVisualLinewise) {
                std::swap(m_vimVisualAnchorLine, m_vimVisualCursorLine);
                vimNormalizeLinewiseSelection();
            }
            break;
        }
        case 'x':
        case 'd':
            if (hasSelectionAt(0)) {
                vimDeleteRange(selectionMinAt(0), selectionMaxAt(0));
            }
            m_vimMode = VimMode::Normal;
            m_vimVisualLinewise = false;
            break;
        case 'y':
            if (hasSelectionAt(0)) {
                /* A linewise yank pastes as whole new lines, so p/P need
                 * to be told which kind this was. */
                vimYankRange(selectionMinAt(0), selectionMaxAt(0), m_vimVisualLinewise);
            }
            collapseToOneCursor();
            m_vimMode = VimMode::Normal;
            m_vimVisualLinewise = false;
            break;
        case 'c':
            if (hasSelectionAt(0)) {
                vimChangeRange(selectionMinAt(0), selectionMaxAt(0));
            } else {
                m_vimMode = VimMode::Insert;
            }
            m_vimVisualLinewise = false;
            break;
        default:
            break; /* unrecognized in Visual: swallowed, no state change */
        }
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    /* Normal-mode-only from here: operators, x/p/P/u, mode entry. */

    if (c == 'd' || c == 'y' || c == 'c') {
        if (m_vimPendingOperator == c) {
            vimApplyPendingOperatorLinewise(lineForOffset(m_cursors[0]), count);
        } else if (m_vimPendingOperator == '\0') {
            m_vimPendingOperator = c;
        } else {
            resetVimPendingState();
        }
        ensureCursorVisible();
        update();
        return true;
    }

    if (m_vimPendingOperator != '\0') {
        /* An operator is pending but this key is neither the same
         * operator repeated nor a recognized motion — an invalid
         * combo. Real vim also just does nothing here. */
        resetVimPendingState();
        return true;
    }

    switch (c) {
    case 'v':
        m_vimMode = VimMode::Visual;
        m_vimVisualLinewise = false;
        break;
    case 'V':
        m_vimMode = VimMode::Visual;
        m_vimVisualLinewise = true;
        m_vimVisualAnchorLine = lineForOffset(m_cursors[0]);
        m_vimVisualCursorLine = m_vimVisualAnchorLine;
        vimNormalizeLinewiseSelection();
        break;
    case 'i':
        m_vimMode = VimMode::Insert;
        break;
    case 'a':
        moveCursorRightAt(0, false);
        m_vimMode = VimMode::Insert;
        break;
    case 'I': {
        size_t target = vimFirstNonBlank(lineForOffset(m_cursors[0]));
        m_cursors[0] = target;
        m_selectionAnchors[0] = target;
        m_vimMode = VimMode::Insert;
        break;
    }
    case 'A':
        moveCursorEndAt(0, false);
        m_vimMode = VimMode::Insert;
        break;
    case 'o':
        moveCursorEndAt(0, false);
        insertText(QByteArrayLiteral("\n"));
        m_vimMode = VimMode::Insert;
        break;
    case 'O':
        vimOpenLineAbove();
        m_vimMode = VimMode::Insert;
        break;
    case 'x': {
        size_t start = m_cursors[0];
        size_t end = start;
        for (int n = 0; n < count && end < static_cast<size_t>(m_cache.size()); ++n) {
            end = vimNextCharBoundary(end);
        }
        if (end > start) {
            vimDeleteRange(start, end);
        }
        break;
    }
    case 'p':
        vimPasteAfter();
        break;
    case 'P':
        vimPasteBefore();
        break;
    case 'u':
        undo();
        break;
    default:
        break; /* unrecognized: swallowed, no state change */
    }

    resetVimPendingState();
    ensureCursorVisible();
    update();
    return true;
}
