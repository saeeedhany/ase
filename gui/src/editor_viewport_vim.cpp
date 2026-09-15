#include "editor_viewport.h"

#include "editor_viewport_internal.h"
#include "vim_register.h"

#include "command_line.h"

#include <algorithm>
#include <utility>

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>

/* ------------------------------------------------------------- Vim mode */
/* Everything below operates on m_cursors[0] only; any Vim key
 * collapses to one cursor first. See docs/adr/0046. */

/* Pushes anchor and cursor to the outer edges of the two lines, so
 * nothing downstream needs to know linewise Visual exists. */
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
    m_vimPendingFind = '\0';
    m_vimPendingReplace = false;
}

/* '\n' counts as Blank, which is what lets w/b/e cross lines with no
 * special-casing. */
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

bool EditorViewport::vimLineIsEmpty(int line) const {
    if (line < 0 || line >= m_lineStarts.size()) {
        return true;
    }
    int start = m_lineStarts[line];
    int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : m_cache.size();
    /* Zero-length only: a line of spaces is part of the paragraph. */
    return end <= start;
}

/* `t`/`T` stop one short of the target. A count fails as a unit: no
 * third x means no move at all, not a move to the second.
 *
 * The operator range is inclusive of the character landed on when
 * searching forward, exclusive of the cursor's old position going
 * backward. See docs/adr/0069. */
void EditorViewport::vimApplyFindInLine(char command, char target, int count) {
    size_t before = m_cursors[0];
    size_t after = vimFindInLine(command, target, count);
    if (after == before) {
        /* Not on this line; a miss is a no-op. */
        resetVimPendingState();
        return;
    }

    if (m_vimPendingOperator != '\0') {
        bool forward = (command == 'f' || command == 't');
        size_t start = forward ? before : after;
        size_t end = forward ? vimNextCharBoundary(after) : before;
        vimApplyPendingOperatorCharwise(start, end);
        return;
    }

    m_cursors[0] = after;
    if (m_vimMode != VimMode::Visual) {
        m_selectionAnchors[0] = after;
    }
    vimNormalizeLinewiseSelection();
    resetVimPendingState();
    ensureCursorVisible();
    resetCaretBlink();
    update();
}

size_t EditorViewport::vimFindInLine(char command, char target, int count) const {
    size_t cursor = m_cursors[0];
    int line = lineForOffset(cursor);
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : m_cache.size();

    bool forward = (command == 'f' || command == 't');
    bool till = (command == 't' || command == 'T');
    int at = static_cast<int>(cursor);

    for (int n = 0; n < count; ++n) {
        /* One further out than `f`, or a repeat never moves. */
        int from = at + (forward ? 1 : -1);
        if (till && n > 0) {
            from += forward ? 1 : -1;
        }
        int found = -1;
        for (int i = from; forward ? (i < lineEnd) : (i >= lineStart); i += forward ? 1 : -1) {
            if (i >= lineStart && i < lineEnd && m_cache[i] == target) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            return cursor; /* no such character: the whole motion is a no-op */
        }
        at = found;
    }

    if (till) {
        at += forward ? -1 : 1;
    }
    return static_cast<size_t>(std::clamp(at, lineStart, lineEnd));
}

size_t EditorViewport::vimParagraphForward(size_t pos) const {
    int line = lineForOffset(pos);
    for (int i = line + 1; i < m_lineStarts.size(); ++i) {
        if (vimLineIsEmpty(i)) {
            return static_cast<size_t>(m_lineStarts[i]);
        }
    }
    return static_cast<size_t>(m_cache.size());
}

size_t EditorViewport::vimParagraphBackward(size_t pos) const {
    int line = lineForOffset(pos);
    for (int i = line - 1; i >= 0; --i) {
        if (vimLineIsEmpty(i)) {
            return static_cast<size_t>(m_lineStarts[i]);
        }
    }
    return 0;
}

/* Cursor and viewport move together, so the cursor keeps its screen
 * row and the text slides under it. */
void EditorViewport::vimHalfPageMotion(int direction) {
    if (m_lineHeight <= 0 || m_lineStarts.isEmpty()) {
        return;
    }
    int visibleLines = std::max(1, height() / m_lineHeight);
    int half = std::max(1, visibleLines / 2);
    int maxLine = static_cast<int>(m_lineStarts.size()) - 1;
    int line = lineForOffset(m_cursors[0]);
    int delta = std::clamp(line + direction * half, 0, maxLine) - line;
    if (delta == 0) {
        return;
    }

    m_scrollLine = std::clamp(m_scrollLine + delta, 0, maxLine);
    /* Shared vertical-move path, so the sticky column behaves as for
     * j/k. */
    moveCursorVerticallyAt(0, delta, m_vimMode == VimMode::Visual);
    vimNormalizeLinewiseSelection();
    ensureCursorVisible(); /* corrects only if the two ended up out of step */
    resetCaretBlink();
    update();
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
            case '}': {
                size_t target = vimParagraphForward(m_cursors[0]);
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                break;
            }
            case '{': {
                size_t target = vimParagraphBackward(m_cursors[0]);
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

    /* Operator pending: j/k span whole lines, everything else an exact
     * byte range. */
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
            case '}':
                after = vimParagraphForward(after);
                break;
            case '{':
                after = vimParagraphBackward(after);
                break;
            case 'e':
                /* Inclusive of the landed-on char, unlike a plain 'e'. */
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
        vimMarkChange();
        break;
    case 'y':
        vimYankRange(start, end, false);
        break;
    case 'c':
        vimChangeRange(start, end);
        vimMarkChange();
        vimBeginInsertCapture();
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
        vimMarkChange();
        break;
    case 'y':
        vimYankLines(startLine, lineCount);
        break;
    case 'c':
        /* Real vim leaves a blank line here; v1 simplification. */
        vimDeleteLines(startLine, lineCount);
        m_vimMode = VimMode::Insert;
        vimMarkChange();
        vimBeginInsertCapture();
        break;
    default:
        break;
    }
}

/* Normalises a linewise payload to whole newline-terminated lines,
 * however the range was cut. Without it, `dd` on the last line stores
 * "\nfoo" or "foo" and `p` pastes a blank line or joins. */
void EditorViewport::vimSetRegister(const QByteArray &text, bool linewise) {
    QByteArray payload = text;
    if (linewise) {
        if (payload.startsWith('\n')) {
            payload.remove(0, 1);
        }
        if (!payload.endsWith('\n')) {
            payload.append('\n');
        }
    }
    VimRegister::unnamed().set(payload, linewise);
}

void EditorViewport::vimDeleteRange(size_t start, size_t end, bool linewise) {
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    vimSetRegister(removed, linewise);
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    refreshCache();
    ensureCursorVisible();
    update();
}

size_t EditorViewport::vimLinewiseDeleteStart(size_t start, size_t end) const {
    if (end < static_cast<size_t>(m_cache.size()) || start == 0) {
        return start;
    }
    return (m_cache[static_cast<int>(start) - 1] == '\n') ? start - 1 : start;
}

void EditorViewport::vimYankRange(size_t start, size_t end, bool linewise) {
    QByteArray text = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    /* A linewise yank of the last line has no newline to take, and
     * would paste as a fragment. */
    vimSetRegister(text, linewise);
    /* Yank mirrors to the system clipboard; delete does not. Deleting
     * should not wipe what you last copied elsewhere. */
    QGuiApplication::clipboard()->setText(QString::fromUtf8(VimRegister::unnamed().text()));
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimChangeRange(size_t start, size_t end, bool linewise) {
    vimDeleteRange(start, end, linewise);
    m_vimMode = VimMode::Insert;
}

void EditorViewport::vimDeleteLines(int startLine, int count) {
    startLine = std::clamp(startLine, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int endLine = std::clamp(startLine + count - 1, startLine, static_cast<int>(m_lineStarts.size()) - 1);
    size_t start = static_cast<size_t>(m_lineStarts[startLine]);
    /* Through the start of the next line, so no blank is left behind. */
    bool throughLastLine = (endLine + 1 >= m_lineStarts.size());
    size_t end = throughLastLine ? static_cast<size_t>(m_cache.size())
                                 : static_cast<size_t>(m_lineStarts[endLine + 1]);
    /* Without this, `dd` on the last line of a newline-terminated file
     * deletes a zero-length range and does nothing. */
    start = vimLinewiseDeleteStart(start, end);
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    vimSetRegister(removed, true);
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
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
    /* The last line stores no newline, so without this `yy` there
     * yanks an empty string and `p` pastes nothing. */
    vimSetRegister(text, true);
    QGuiApplication::clipboard()->setText(QString::fromUtf8(VimRegister::unnamed().text()));
    size_t target = vimFirstNonBlank(startLine);
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimPasteAfter() {
    const VimRegister &reg = VimRegister::unnamed();
    if (reg.isEmpty()) {
        /* Deliberately no clipboard fallback: `p` always means the
         * register. Ctrl+V pastes the clipboard. */
        return;
    }
    QByteArray bytes = reg.text();
    bool linewise = reg.isLinewise();
    size_t insertAt;
    if (linewise) {
        int line = lineForOffset(m_cursors[0]);
        insertAt = (line + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[line + 1])
                                                     : static_cast<size_t>(m_cache.size());
        if (insertAt == static_cast<size_t>(m_cache.size())) {
            if (!bytes.endsWith('\n')) {
                bytes.append('\n');
            }
            /* A buffer not ending in a newline has nothing to append
             * after ("a\nb" + p gave "a\nbb"). Trade the trailing
             * newline for a leading one. */
            if (!m_cache.isEmpty() && m_cache.back() != '\n') {
                bytes.chop(1);
                bytes.prepend('\n');
            }
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
    refreshCache();
    /* Charwise paste leaves the cursor on the *last* character of what
     * was pasted, not the first — real vim's rule, and the one that
     * makes a second `p` continue the text rather than re-paste into
     * the middle of it. Linewise is the opposite and already right:
     * first non-blank of the first pasted line. Reported by an external
     * tester, who expected "the end" for both; see docs/adr/0059. */
    size_t target = linewise
                        ? vimFirstNonBlank(lineForOffset(insertAt))
                        : vimPrevCharBoundary(insertAt + static_cast<size_t>(bytes.size()));
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimPasteBefore() {
    const VimRegister &reg = VimRegister::unnamed();
    if (reg.isEmpty()) {
        return;
    }
    QByteArray bytes = reg.text();
    bool linewise = reg.isLinewise();
    size_t insertAt = linewise ? static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])
                               : m_cursors[0];
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_insert(m_buffer, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()));
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    refreshCache();
    /* Charwise paste leaves the cursor on the *last* character of what
     * was pasted, not the first — real vim's rule, and the one that
     * makes a second `p` continue the text rather than re-paste into
     * the middle of it. Linewise is the opposite and already right:
     * first non-blank of the first pasted line. Reported by an external
     * tester, who expected "the end" for both; see docs/adr/0059. */
    size_t target = linewise
                        ? vimFirstNonBlank(lineForOffset(insertAt))
                        : vimPrevCharBoundary(insertAt + static_cast<size_t>(bytes.size()));
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
    refreshCache();
    ensureCursorVisible();
    update();
}

QString EditorViewport::vimBlockGlyphAt(size_t cursor, int *width, AseHighlightCapture *capture) const {
    int line = lineForOffset(cursor);
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForOffset(cursor, line);
    /* Steps by codepoint, so a multi-byte character is measured whole.
     * Returns `cursor` unchanged at end of line, which is the "no real
     * character here" signal below. */
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

/* Returns true for every key Vim claims, including invalid-but-
 * swallowed ones; false only for keys outside its alphabet. See
 * docs/adr/0046 for the state table. */
void EditorViewport::vimRecordKey(QChar qc) {
    if (m_dotReplaying) {
        return;
    }
    /* A key with nothing pending begins a new command, so whatever was
     * being recorded came to nothing and is dropped. */
    if (m_vimMode != VimMode::Visual && m_vimPendingOperator == '\0' && m_vimCount1 == 0 &&
        m_vimCount2 == 0 && !m_vimPendingG && m_vimPendingFind == '\0' && !m_vimPendingReplace) {
        m_dotRecording.clear();
    }
    m_dotRecording.append(qc);
}

void EditorViewport::vimMarkChange() {
    if (m_dotReplaying) {
        return; /* a repeat must not become the thing repeated */
    }
    m_dotKeys = m_dotRecording;
    m_dotInserted.clear();
}

void EditorViewport::vimBeginInsertCapture() {
    if (m_dotReplaying) {
        return;
    }
    m_dotCapturingInsert = true;
    m_dotInsertBuf.clear();
}

void EditorViewport::vimEndInsertCapture() {
    if (!m_dotCapturingInsert) {
        return;
    }
    m_dotCapturingInsert = false;
    m_dotInserted = m_dotInsertBuf;
    m_dotInsertBuf.clear();
}

/*
 * Replays the recorded keys, then the recorded insert text if the change
 * ended up in Insert mode.
 *
 * A count given to `.` replaces the original one rather than multiplying
 * it, as vim does — so `3x` then `2.` deletes two characters, not six.
 */
void EditorViewport::vimRepeatChange(int count) {
    if (m_dotKeys.isEmpty() || m_dotReplaying) {
        return;
    }

    /* Before replaying, not after: the count that selected this repeat is
     * still pending, and a replayed digit would be appended to it —
     * `2.` of a `3x` accumulated 22 and took the whole line. */
    resetVimPendingState();

    QString keys = m_dotKeys;
    if (count > 1) {
        int digits = 0;
        while (digits < keys.size() && keys.at(digits).isDigit()) {
            digits++;
        }
        keys = QString::number(count) + keys.mid(digits);
    }

    m_dotReplaying = true;
    for (QChar ch : keys) {
        /* key code 0 is fine: the dispatcher reads event->text() for
         * everything except Backspace, which never gets recorded. */
        QKeyEvent replay(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        handleVimNormalOrVisualKey(&replay);
    }
    if (m_vimMode == VimMode::Insert) {
        if (!m_dotInserted.isEmpty()) {
            if (m_vimReplacing) {
                /* Replayed text has to go back through the overwriting
                 * path, or repeating an `R` session would insert. */
                for (int i = 0; i < m_dotInserted.size();) {
                    int len = 1;
                    while (i + len < m_dotInserted.size() &&
                           (static_cast<unsigned char>(m_dotInserted.at(i + len)) & 0xC0) == 0x80) {
                        len++;
                    }
                    vimReplaceTyped(m_dotInserted.mid(i, len));
                    i += len;
                }
            } else {
                insertText(m_dotInserted);
            }
        }
        /* Leave Insert the way Escape does, so the cursor lands where it
         * would have if this had been typed — count passes first, then
         * the step back. */
        if (m_vimReplacing) {
            vimLeaveReplaceMode();
        }
        if (m_cursors[0] > static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])) {
            moveCursorLeftAt(0, false);
        }
        m_vimMode = VimMode::Normal;
    }
    m_dotReplaying = false;

    resetVimPendingState();
    ensureCursorVisible();
    update();
}

/*
 * Replaces `count` characters with `target`, cursor left on the last one
 * — or, for Enter, replaces them all with a single line break, which is
 * what vim does. Does nothing at all unless the line has `count`
 * characters left: a partial replace is not vim's behaviour and would be
 * worse than a no-op.
 *
 * Writes no register. `r` is not a delete, and clobbering the unnamed
 * register with one character would make `p` after it useless.
 */
void EditorViewport::vimReplaceChar(QChar target, int count, bool newline) {
    int line = lineForOffset(m_cursors[0]);
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1
                                                  : static_cast<int>(m_cache.size());

    size_t start = m_cursors[0];
    size_t end = start;
    for (int n = 0; n < count; ++n) {
        if (end >= static_cast<size_t>(lineEnd)) {
            return; /* fewer than `count` characters left on this line */
        }
        end = vimNextCharBoundary(end);
    }
    if (end <= start) {
        return;
    }

    QByteArray inserted = newline ? QByteArrayLiteral("\n") : QString(count, target).toUtf8();
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));

    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    if (ase_buffer_insert(m_buffer, start, inserted.constData(), static_cast<size_t>(inserted.size()))) {
        ase_undo_record_insert(m_undo, start, inserted.constData(), static_cast<size_t>(inserted.size()));
    }
    /* On the last replaced character, as vim leaves it — or just past the
     * break, which is the start of the new line. */
    size_t landing = newline ? start + 1
                             : start + static_cast<size_t>(inserted.size() - QString(target).toUtf8().size());
    m_cursors[0] = landing;
    m_selectionAnchors[0] = landing;
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    refreshCache();
    vimMarkChange();
}

/*
 * Replaces every character of the selection, leaving line breaks alone —
 * a selection spanning two lines stays two lines.
 *
 * Covers exactly the range visual `d` would delete, which is this
 * editor's own selection rather than vim's: the cursor sits *at* a byte
 * here rather than *on* a character, so a selection is exclusive of the
 * character under the cursor where vim's is inclusive. That gap predates
 * this and applies to every visual operator; matching vim here alone
 * would make `r` disagree with the highlight it is acting on. See
 * docs/adr/0078.
 */
void EditorViewport::vimReplaceSelection(QChar target) {
    size_t start = selectionMinAt(0);
    size_t end = selectionMaxAt(0);
    if (start >= end) {
        return;
    }
    QByteArray original = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    QByteArray replacement;
    replacement.reserve(original.size());
    QByteArray one = QString(target).toUtf8();
    for (int i = 0; i < original.size();) {
        if (original.at(i) == '\n') {
            replacement.append('\n');
            i++;
            continue;
        }
        int len = 1;
        while (i + len < original.size() &&
               (static_cast<unsigned char>(original.at(i + len)) & 0xC0) == 0x80) {
            len++;
        }
        replacement.append(one);
        i += len;
    }

    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, original.constData(), static_cast<size_t>(original.size()));
    }
    if (ase_buffer_insert(m_buffer, start, replacement.constData(),
                          static_cast<size_t>(replacement.size()))) {
        ase_undo_record_insert(m_undo, start, replacement.constData(),
                               static_cast<size_t>(replacement.size()));
    }
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    refreshCache();
    vimMarkChange();
}

void EditorViewport::vimEnterReplaceMode(int count) {
    m_vimMode = VimMode::Insert;
    m_vimReplacing = true;
    m_replaceOriginals.clear();
    m_replaceTyped.clear();
    m_replaceCount = std::max(1, count);
    vimMarkChange();
    vimBeginInsertCapture();
}

/*
 * Overwrites the character under the cursor, or appends when the line
 * has run out — `R` at the end of a line goes on typing rather than
 * stopping, which is what vim does and what anyone would expect.
 *
 * One undo group per character, matching the granularity Insert mode
 * already has.
 */
void EditorViewport::vimReplaceTyped(const QByteArray &bytes) {
    size_t cursor = m_cursors[0];
    int line = lineForOffset(cursor);
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1
                                                   : static_cast<int>(m_cache.size());

    /* A line break is inserted, never overwritten: there is no character
     * "under" the cursor to trade for it. */
    bool overwrite = !bytes.startsWith('\n') && cursor < static_cast<size_t>(lineEnd);
    size_t end = overwrite ? vimNextCharBoundary(cursor) : cursor;
    QByteArray original =
        overwrite ? m_cache.mid(static_cast<int>(cursor), static_cast<int>(end - cursor)) : QByteArray();

    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (overwrite && ase_buffer_delete(m_buffer, cursor, end - cursor)) {
        ase_undo_record_delete(m_undo, cursor, original.constData(), static_cast<size_t>(original.size()));
    }
    if (ase_buffer_insert(m_buffer, cursor, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, cursor, bytes.constData(), static_cast<size_t>(bytes.size()));
        cursor += static_cast<size_t>(bytes.size());
    }
    m_cursors[0] = cursor;
    m_selectionAnchors[0] = cursor;
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));

    m_replaceOriginals.push_back(original);
    m_replaceTyped.append(bytes);
    refreshCache();
}

/* True when it consumed the key. Before the start of the session there
 * is nothing of ours to undo, so the key is left alone. */
bool EditorViewport::vimReplaceBackspace() {
    if (m_replaceOriginals.isEmpty()) {
        return false;
    }
    QByteArray original = m_replaceOriginals.takeLast();
    size_t cursor = m_cursors[0];
    /* What was typed last is immediately before the cursor; step back
     * over it by codepoint rather than by byte. */
    size_t start = vimPrevCharBoundary(cursor);
    if (start >= cursor) {
        return false;
    }
    QByteArray typed = m_cache.mid(static_cast<int>(start), static_cast<int>(cursor - start));

    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_delete(m_buffer, start, cursor - start)) {
        ase_undo_record_delete(m_undo, start, typed.constData(), static_cast<size_t>(typed.size()));
    }
    if (!original.isEmpty() &&
        ase_buffer_insert(m_buffer, start, original.constData(), static_cast<size_t>(original.size()))) {
        ase_undo_record_insert(m_undo, start, original.constData(), static_cast<size_t>(original.size()));
    }
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));

    if (!m_replaceTyped.isEmpty()) {
        m_replaceTyped.chop(static_cast<int>(cursor - start));
    }
    refreshCache();
    return true;
}

/* A count repeats the typed text on the way out — `3Rab` leaves ababab,
 * replacing as it goes. */
void EditorViewport::vimLeaveReplaceMode() {
    QByteArray typed = m_replaceTyped;
    for (int n = 1; n < m_replaceCount && !typed.isEmpty(); ++n) {
        for (int i = 0; i < typed.size();) {
            size_t before = m_cursors[0];
            int len = 1;
            while (i + len < typed.size() &&
                   (static_cast<unsigned char>(typed.at(i + len)) & 0xC0) == 0x80) {
                len++;
            }
            vimReplaceTyped(typed.mid(i, len));
            i += len;
            if (m_cursors[0] == before) {
                break; /* made no progress; do not spin */
            }
        }
    }
    m_vimReplacing = false;
    m_replaceOriginals.clear();
    m_replaceTyped.clear();
    m_replaceCount = 1;
}

/* An f/F/t/T target, or the second half of a `g` pair. */
bool EditorViewport::vimResolvePendingKey(QChar qc, int key) {
    /* Mid-`r`: this key is the replacement, whatever it is. Escape never
     * reaches here — keyPressEvent takes it first and resets the pending
     * state, which is how `r<Esc>` cancels. */
    if (m_vimPendingReplace) {
        m_vimPendingReplace = false;
        /* The key code, not the text: Return arrives as U+0000 under the
         * offscreen platform plugin and as "\r" under X11, so the text is
         * not something to branch on. Any other non-printable target
         * cancels, as it does in vim. */
        bool newline = (key == Qt::Key_Return || key == Qt::Key_Enter);
        if (m_vimMode == VimMode::Visual) {
            if (qc.isPrint()) {
                vimReplaceSelection(qc);
            }
            collapseToOneCursor();
            m_vimMode = VimMode::Normal;
            m_vimVisualLinewise = false;
        } else if (newline || qc.isPrint()) {
            vimReplaceChar(qc, std::max(1, m_vimCount1) * std::max(1, m_vimCount2), newline);
        }
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    /* This key is the target, whatever it is: `f3` and `fd` search for
     * '3' and 'd', not a count or an operator. */
    if (m_vimPendingFind != '\0') {
        char command = m_vimPendingFind;
        m_vimPendingFind = '\0';
        char target = qc.toLatin1();
        if (target == '\0') {
            resetVimPendingState(); /* non-Latin1 target: nothing to search for */
            return true;
        }
        m_vimLastFindCommand = command;
        m_vimLastFindTarget = target;
        vimApplyFindInLine(command, target, std::max(1, m_vimCount1) * std::max(1, m_vimCount2));
        return true;
    }

    /* Mid-"g": resolved on the very next key, whatever it is. */
    if (m_vimPendingG) {
        m_vimPendingG = false;
        if (qc == QLatin1Char('g')) {
            int targetLine = (m_vimCount1 > 0) ? (m_vimCount1 - 1) : 0;
            if (m_vimPendingOperator == '\0') {
                recordJump();
            }
            vimPrepareLinewiseMotion();
            vimGotoLine(targetLine);
        } else if (qc == QLatin1Char('d')) {
            /* The language server's answer, not vim's local scan. Not
             * a motion, so no operator can be pending. */
            resetVimPendingState();
            goToDefinition();
            return true;
        }
        vimNormalizeLinewiseSelection();
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }
    return false;
}

/* True when the key was consumed as part of a count. */
bool EditorViewport::vimAccumulateCount(QChar qc) {
    /* A bare '0' is the column-0 motion, not a digit. */
    if (qc.isDigit()) {
        int d = qc.digitValue();
        int &count = (m_vimPendingOperator != '\0') ? m_vimCount2 : m_vimCount1;
        if (!(d == 0 && count == 0)) {
            count = count * 10 + d;
            return true;
        }
    }
    return false;
}

/* Motions, and the keys that arm one. False if `c` is neither. */
bool EditorViewport::vimApplyMotionKey(char c, int count) {
    if (c == ':') {
        /* Normal/Visual only: Insert still needs `:` as a literal. */
        if (m_commandLine != nullptr) {
            m_commandLine->openPrompt(QLatin1Char(':'));
        }
        resetVimPendingState();
        return true;
    }
    if (c == '/' || c == '?') {
        if (m_commandLine != nullptr) {
            m_commandLine->openPrompt(QLatin1Char(c));
        }
        resetVimPendingState();
        return true;
    }
    if (c == 'n' || c == 'N') {
        /* `n` keeps the search's own direction, `N` reverses it, so `n`
         * after `?` goes backward. */
        searchRepeat(c == 'n' ? searchWasForward() : !searchWasForward());
        resetVimPendingState();
        return true;
    }
    if (c == 'f' || c == 'F' || c == 't' || c == 'T') {
        /* Keeps the pending count, so `3fx` is still the third x. */
        m_vimPendingFind = c;
        return true;
    }
    if (c == ';' || c == ',') {
        if (m_vimLastFindCommand == '\0') {
            resetVimPendingState();
            return true;
        }
        /* Same search, reversed — why this is stored as a letter. */
        char command = m_vimLastFindCommand;
        if (c == ',') {
            switch (command) {
            case 'f': command = 'F'; break;
            case 'F': command = 'f'; break;
            case 't': command = 'T'; break;
            case 'T': command = 't'; break;
            default: break;
            }
        }
        vimApplyFindInLine(command, m_vimLastFindTarget, count);
        return true;
    }
    if (c == 'g') {
        m_vimPendingG = true;
        return true;
    }
    if (c == 'G') {
        int targetLine = (m_vimCount1 > 0) ? (m_vimCount1 - 1) : static_cast<int>(m_lineStarts.size()) - 1;
        if (m_vimPendingOperator == '\0') {
            recordJump(); /* a jump; with an operator pending it is a range, not a move */
        }
        vimPrepareLinewiseMotion();
        vimGotoLine(targetLine);
        vimNormalizeLinewiseSelection();
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }
    if (c == 'h' || c == 'l' || c == 'j' || c == 'k' || c == '0' || c == '^' || c == '$' || c == 'w' ||
        c == 'b' || c == 'e' || c == '{' || c == '}') {
        if ((c == '{' || c == '}') && m_vimPendingOperator == '\0') {
            /* Far enough to be worth coming back from; h/j/k/l are
             * deliberately not. See docs/adr/0070. */
            recordJump();
        }
        /* A pure motion glides, like every other navigation here. */
        vimPrepareLinewiseMotion();
        vimExecuteMotion(c, count);
        vimNormalizeLinewiseSelection();
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }
    return false;
}

void EditorViewport::vimApplyVisualKey(char c) {
    switch (c) {
    case 'v':
        /* v in linewise Visual drops to charwise, not out. */
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
        /* Other end of the selection, keeping it. */
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
            size_t start = selectionMinAt(0);
            size_t end = selectionMaxAt(0);
            /* No trailing newline at the buffer's end, so this
             * emptied the last line instead of removing it. */
            if (m_vimVisualLinewise) {
                start = vimLinewiseDeleteStart(start, end);
            }
            vimDeleteRange(start, end, m_vimVisualLinewise);
            if (m_vimVisualLinewise) {
                /* The range began on the previous line's newline,
                 * so land on the first non-blank instead. */
                size_t target = vimFirstNonBlank(lineForOffset(m_cursors[0]));
                m_cursors[0] = target;
                m_selectionAnchors[0] = target;
            }
        }
        m_vimMode = VimMode::Normal;
        m_vimVisualLinewise = false;
        break;
    case 'y':
        if (hasSelectionAt(0)) {
            /* p/P need to know which kind this was. */
            vimYankRange(selectionMinAt(0), selectionMaxAt(0), m_vimVisualLinewise);
        }
        collapseToOneCursor();
        m_vimMode = VimMode::Normal;
        m_vimVisualLinewise = false;
        break;
    case 'c':
        if (hasSelectionAt(0)) {
            /* Not extended over the preceding newline as `d` is, or
             * the insertion point lands on the previous line. */
            vimChangeRange(selectionMinAt(0), selectionMaxAt(0), m_vimVisualLinewise);
        } else {
            m_vimMode = VimMode::Insert;
        }
        m_vimVisualLinewise = false;
        break;
    case 'r':
        /* Early: the tail below would reset the pending flag before the
         * replacement character ever arrived. */
        m_vimPendingReplace = true;
        return;
    default:
        break; /* unrecognized in Visual: swallowed, no state change */
    }
    resetVimPendingState();
    ensureCursorVisible();
    update();
}

void EditorViewport::vimApplyNormalKey(char c, int count) {
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
        return;
    }

    if (m_vimPendingOperator != '\0') {
        /* Operator pending, but this is neither a repeat nor a motion. */
        resetVimPendingState();
        return;
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
        vimMarkChange();
        vimBeginInsertCapture();
        break;
    case 'a':
        moveCursorRightAt(0, false);
        m_vimMode = VimMode::Insert;
        vimMarkChange();
        vimBeginInsertCapture();
        break;
    case 'I': {
        size_t target = vimFirstNonBlank(lineForOffset(m_cursors[0]));
        m_cursors[0] = target;
        m_selectionAnchors[0] = target;
        m_vimMode = VimMode::Insert;
        vimMarkChange();
        vimBeginInsertCapture();
        break;
    }
    case 'A':
        moveCursorEndAt(0, false);
        m_vimMode = VimMode::Insert;
        vimMarkChange();
        vimBeginInsertCapture();
        break;
    case 'o':
        moveCursorEndAt(0, false);
        insertText(QByteArrayLiteral("\n"));
        m_vimMode = VimMode::Insert;
        vimMarkChange();
        vimBeginInsertCapture();
        break;
    case 'O':
        vimOpenLineAbove();
        m_vimMode = VimMode::Insert;
        vimMarkChange();
        vimBeginInsertCapture();
        break;
    case 'x': {
        size_t start = m_cursors[0];
        size_t end = start;
        for (int n = 0; n < count && end < static_cast<size_t>(m_cache.size()); ++n) {
            end = vimNextCharBoundary(end);
        }
        if (end > start) {
            vimDeleteRange(start, end);
            vimMarkChange();
        }
        break;
    }
    case 'r':
        m_vimPendingReplace = true;
        return; /* the count is still needed when the target arrives */
    case 'R':
        vimEnterReplaceMode(count);
        break;
    case 'p':
        vimPasteAfter();
        vimMarkChange();
        break;
    case 'P':
        vimPasteBefore();
        vimMarkChange();
        break;
    case 'u':
        undo(); /* not a change: `.` after `u` repeats what `u` undid */
        break;
    case '.':
        vimRepeatChange(count);
        break;
    default:
        break; /* unrecognized: swallowed, no state change */
    }

    resetVimPendingState();
    ensureCursorVisible();
    update();
}

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
    vimRecordKey(qc);

    if (vimResolvePendingKey(qc, event->key())) {
        return true;
    }
    if (vimAccumulateCount(qc)) {
        return true;
    }

    char c = qc.toLatin1(); /* '\0' for non-Latin1 — falls through to "unrecognized" below. */
    int count = std::max(1, m_vimCount1) * std::max(1, m_vimCount2);

    if (vimApplyMotionKey(c, count)) {
        return true;
    }

    if (m_vimMode == VimMode::Visual) {
        vimApplyVisualKey(c);
    } else {
        vimApplyNormalKey(c, count);
    }
    return true;
}
