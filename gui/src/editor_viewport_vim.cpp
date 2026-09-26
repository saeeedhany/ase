#include "editor_viewport.h"

#include "editor_viewport_internal.h"

#include "keybindings.h"
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

    /* Both ends sit at the start of their own line.
     *
     * The cursor used to be parked at the start of the line *after* the
     * last selected one, because that is where the selection's range
     * ends. The range was right and the caret was one line below what
     * was highlighted — pressing V looked like it selected a line and
     * jumped off it, and the status bar agreed. vimVisualEnd() works
     * the span out from the line numbers instead, so the cursor no
     * longer has to carry it. See docs/adr/0122. */
    (void)lineEnd;
    m_selectionAnchors[0] = lineStart(anchorLine);
    m_cursors[0] = lineStart(cursorLine);
}

void EditorViewport::resetVimPendingState() {
    m_vimPending.reset();
    /* The command resolved (or was abandoned), so there is nothing
     * half-typed left to show. */
    if (!m_vimPendingKeys.isEmpty()) {
        m_vimPendingKeys.clear();
        emit pendingInputChanged(m_vimPendingKeys);
    }
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

EditorViewport::VimCharClass EditorViewport::vimClassifyAt(size_t pos, bool big) const {
    VimCharClass cls = vimClassifyAt(pos);
    if (big && cls == VimCharClass::Punct) {
        return VimCharClass::Word;
    }
    return cls;
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

size_t EditorViewport::vimWordForward(size_t pos, bool big) const {
    size_t len = static_cast<size_t>(m_cache.size());
    if (pos >= len) {
        return len;
    }
    VimCharClass start = vimClassifyAt(pos, big);
    if (start != VimCharClass::Blank) {
        while (pos < len && vimClassifyAt(pos, big) == start) {
            pos = vimNextCharBoundary(pos);
        }
    }
    while (pos < len && vimClassifyAt(pos, big) == VimCharClass::Blank) {
        pos = vimNextCharBoundary(pos);
    }
    return pos;
}

size_t EditorViewport::vimWordRunEnd(size_t pos, bool big) const {
    size_t len = static_cast<size_t>(m_cache.size());
    if (pos >= len) {
        return pos;
    }
    VimCharClass cls = vimClassifyAt(pos, big);
    size_t p = pos;
    while (p < len && vimClassifyAt(p, big) == cls) {
        p = vimNextCharBoundary(p);
    }
    return p;
}

size_t EditorViewport::vimWordEnd(size_t pos, bool big) const {
    size_t len = static_cast<size_t>(m_cache.size());
    if (pos >= len) {
        return pos;
    }
    pos = vimNextCharBoundary(pos);
    while (pos < len && vimClassifyAt(pos, big) == VimCharClass::Blank) {
        pos = vimNextCharBoundary(pos);
    }
    if (pos >= len) {
        return len;
    }
    VimCharClass cls = vimClassifyAt(pos, big);
    size_t next = vimNextCharBoundary(pos);
    while (next < len && vimClassifyAt(next, big) == cls) {
        pos = next;
        next = vimNextCharBoundary(next);
    }
    return pos;
}

size_t EditorViewport::vimWordBackward(size_t pos, bool big) const {
    if (pos == 0) {
        return 0;
    }
    pos = vimPrevCharBoundary(pos);
    while (pos > 0 && vimClassifyAt(pos, big) == VimCharClass::Blank) {
        pos = vimPrevCharBoundary(pos);
    }
    if (vimClassifyAt(pos, big) == VimCharClass::Blank) {
        return 0;
    }
    VimCharClass cls = vimClassifyAt(pos, big);
    while (pos > 0) {
        size_t prev = vimPrevCharBoundary(pos);
        if (vimClassifyAt(prev, big) != cls) {
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

    if (m_vimPending.op != '\0') {
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

bool EditorViewport::vimLineBelow(int fromLine, int count, int *target) const {
    int line = std::clamp(fromLine + count - 1, 0, vimLastLine());
    if (count > 1 && line == fromLine) {
        return false;
    }
    *target = line;
    return true;
}

int EditorViewport::vimLastLine() const {
    int last = static_cast<int>(m_lineStarts.size()) - 1;
    if (last > 0 && static_cast<size_t>(m_lineStarts[last]) >= static_cast<size_t>(m_cache.size())) {
        last--;
    }
    return last;
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
    if (m_vimPending.op != '\0') {
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

    if (m_vimPending.op == '\0') {
        for (int n = 0; n < count; ++n) {
            switch (m) {
            case 'h':
                /* vim's `h` stops at column 1; the arrow key it shares a
                 * primitive with wraps to the line above. */
                if (m_cursors[0] > static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])) {
                    moveCursorLeftAt(0, visual);
                }
                break;
            case 'l':
                /* And `l` stops at the last character, rather than
                 * stepping onto the newline and then the next line. */
                if (vimNextCharBoundary(m_cursors[0]) <
                    vimLineEndOffset(lineForOffset(m_cursors[0]))) {
                    moveCursorRightAt(0, visual);
                }
                break;
            case 'j':
                /* Not past the last real line: the position after a
                 * trailing newline is a line the arrow keys can reach
                 * and `j` cannot, the same split `l` has. */
                if (lineForOffset(m_cursors[0]) < vimLastLine()) {
                    moveCursorVerticallyAt(0, 1, visual);
                }
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
            case '%': {
                size_t target = vimMatchBracket(m_cursors[0]);
                if (target != m_cursors[0]) {
                    recordJump(); /* far enough to want to come back from */
                    m_cursors[0] = target;
                    if (!visual) {
                        m_selectionAnchors[0] = target;
                    }
                }
                return;
            }
            case '$': {
                if (n == 0 && count > 1) {
                    /* Once, with the count, rather than once per repeat:
                     * `$` is idempotent, so the loop alone would leave
                     * `3$` meaning `$`. */
                    int target = lineForOffset(m_cursors[0]);
                    vimLineBelow(target, count, &target);
                    m_cursors[0] = static_cast<size_t>(m_lineStarts[target]);
                    if (!visual) {
                        m_selectionAnchors[0] = m_cursors[0];
                    }
                }
                moveCursorEndAt(0, visual);
                /* vim leaves the cursor on the last character, not past
                 * it. The operator branch below computes `$` for itself,
                 * so `d$` still reaches the line end. */
                int line = lineForOffset(m_cursors[0]);
                size_t lineStart = static_cast<size_t>(m_lineStarts[line]);
                if (m_cursors[0] > lineStart) {
                    size_t back = vimPrevCharBoundary(m_cursors[0]);
                    m_cursors[0] = back;
                    if (!visual) {
                        m_selectionAnchors[0] = back;
                    }
                }
                return;
            }
            case 'w':
            case 'W': {
                size_t target = vimWordForward(m_cursors[0], m == 'W');
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                break;
            }
            case 'b':
            case 'B': {
                size_t target = vimWordBackward(m_cursors[0], m == 'B');
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                break;
            }
            case 'e':
            case 'E': {
                size_t target = vimWordEnd(m_cursors[0], m == 'E');
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
        /* vimLastLine, not m_lineStarts.size(): the entry past a trailing
         * newline is not a line to operate on. */
        int targetLine = std::clamp(beforeLine + deltaLines, 0, vimLastLine());
        /* A count past the end clamps, but a motion that cannot move at
         * all fails the whole operator — `dj` on the last line does
         * nothing rather than deleting it. */
        if (targetLine == beforeLine) {
            resetVimPendingState();
            return;
        }
        int startLine = std::min(beforeLine, targetLine);
        int lineCount = std::abs(targetLine - beforeLine) + 1;
        vimApplyPendingOperatorLinewise(startLine, lineCount);
        return;
    }

    if (m == '%') {
        size_t target = vimMatchBracket(before);
        if (target == before) {
            resetVimPendingState();
            return;
        }
        vimApplyPendingOperatorCharwise(std::min(before, target),
                                        vimNextCharBoundary(std::max(before, target)));
        return;
    }

    size_t after = before;
    if (m == '0') {
        after = static_cast<size_t>(m_lineStarts[lineForOffset(before)]);
    } else if (m == '^') {
        after = vimFirstNonBlank(lineForOffset(before));
    } else if (m == '$') {
        int target = 0;
        if (!vimLineBelow(lineForOffset(before), count, &target)) {
            resetVimPendingState();
            return;
        }
        after = vimLineEndOffset(target);
    } else {
        /*
         * vim's one special case, and the reason `cw` is not `dw` with a
         * different operator: on a non-blank it changes to the end of
         * the word rather than to the start of the next, so the space
         * after it survives. On whitespace it stays a plain `w`.
         *
         * "End of the word" is the end of the run the cursor is in, not
         * where `e` would go: `e` steps on to the next word when it is
         * already at a run's end, so on `.ab` it reaches the end of `ab`
         * while `cw` changes only the `.`. Only the first step is
         * special; the rest of a count behave like `e`.
         */
        bool changeToWordEnd = ((m == 'w' || m == 'W') && m_vimPending.op == 'c' &&
                                before < static_cast<size_t>(m_cache.size()) &&
                                vimClassifyAt(before) != VimCharClass::Blank);
        for (int n = 0; n < count; ++n) {
            switch (m) {
            case 'h':
                if (after > static_cast<size_t>(m_lineStarts[lineForOffset(after)])) {
                    after = vimPrevCharBoundary(after);
                }
                break;
            case 'l':
                /* `d10l` on a short line deletes to the end of it, not
                 * into the next one. */
                if (after < vimLineEndOffset(lineForOffset(after))) {
                    after = vimNextCharBoundary(after);
                }
                break;
            case 'w':
            case 'W':
                if (!changeToWordEnd) {
                    after = vimWordForward(after, m == 'W');
                } else if (n == 0) {
                    after = vimWordRunEnd(after, m == 'W');
                } else {
                    after = vimNextCharBoundary(vimWordEnd(after, m == 'W'));
                }
                break;
            case 'b':
            case 'B':
                after = vimWordBackward(after, m == 'B');
                break;
            case '}':
                after = vimParagraphForward(after);
                break;
            case '{':
                after = vimParagraphBackward(after);
                break;
            case 'e':
            case 'E':
                /* Inclusive of the landed-on char, unlike a plain 'e'. */
                after = vimNextCharBoundary(vimWordEnd(after, m == 'E'));
                break;
            default:
                break;
            }
        }
    }
    /* vim: when the last word an operator moves over ends a line, the
     * operated text ends there too — `dw` on a line's last word empties
     * it rather than pulling the next line up. */
    if (m == 'w' && after > before) {
        size_t scan = after;
        while (scan > before && (m_cache[static_cast<int>(scan) - 1] == ' ' ||
                                 m_cache[static_cast<int>(scan) - 1] == '\t')) {
            scan--;
        }
        if (scan > before && m_cache[static_cast<int>(scan) - 1] == '\n') {
            after = scan - 1;
        }
    }

    size_t start = std::min(before, after);
    size_t end = std::max(before, after);
    vimApplyPendingOperatorCharwise(start, end);
}

void EditorViewport::vimApplyPendingOperatorCharwise(size_t start, size_t end) {
    char op = m_vimPending.op;
    m_vimRegisterInUse = m_vimPending.registerName;
    resetVimPendingState();
    if (start >= end) {
        return;
    }
    switch (op) {
    case 'd': {
        /* vim turns a charwise delete covering whole lines — column 0 to
         * a line end, across more than one — into a linewise one, so the
         * lines go rather than leaving a blank. */
        int startLine = lineForOffset(start);
        int endLine = lineForOffset(end);
        bool wholeLines = endLine > startLine &&
                          start == static_cast<size_t>(m_lineStarts[startLine]) &&
                          end == vimLineEndOffset(endLine);
        if (wholeLines) {
            vimDeleteLines(startLine, endLine - startLine + 1);
        } else {
            vimDeleteRange(start, end);
        }
        vimMarkChange();
        break;
    }
    case 'y':
        vimYankRange(start, end, false);
        break;
    case '>':
    case '<': {
        /* `>` is always linewise, whatever motion delimited it. */
        int startLine = lineForOffset(start);
        vimIndentLines(startLine, lineForOffset(end) - startLine + 1, op == '>');
        break;
    }
    case 'c':
        /* Before the deletion: vim undoes a `c` and the typing that
         * follows it as one. */
        beginUndoSession();
        vimChangeRange(start, end);
        vimMarkChange();
        vimBeginInsertCapture();
        break;
    default:
        break;
    }
}

void EditorViewport::vimApplyPendingOperatorLinewise(int startLine, int lineCount) {
    char op = m_vimPending.op;
    m_vimRegisterInUse = m_vimPending.registerName;
    resetVimPendingState();
    switch (op) {
    case 'd':
        vimDeleteLines(startLine, lineCount);
        vimMarkChange();
        break;
    case 'y':
        vimYankLines(startLine, lineCount);
        break;
    case '>':
    case '<':
        vimIndentLines(startLine, lineCount, op == '>');
        break;
    case 'c': {
        /* Empties the lines and keeps one to type on, rather than
         * removing them: `cc` is a change, not a delete, and vim leaves
         * you on a blank line. Taking the content but not the final
         * newline is what collapses the span to that one line. */
        beginUndoSession();
        size_t start = static_cast<size_t>(m_lineStarts[startLine]);
        int lastLine = std::min(startLine + lineCount - 1, vimLastLine());
        size_t end = vimLineEndOffset(lastLine);
        if (end > start) {
            vimDeleteRange(start, end, true);
        }
        m_vimMode = VimMode::Insert;
        vimMarkChange();
        vimBeginInsertCapture();
        break;
    }
    default:
        break;
    }
}

/* Normalises a linewise payload to whole newline-terminated lines,
 * however the range was cut. Without it, `dd` on the last line stores
 * "\nfoo" or "foo" and `p` pastes a blank line or joins. */
/* The operator helpers clear the pending state before they run, so they
 * park the name in m_vimRegisterInUse first; everything else still has
 * m_vimPending.registerName live. Either way it is consumed once. */
char EditorViewport::vimTakeRegister() {
    char name = (m_vimRegisterInUse != '\0') ? m_vimRegisterInUse : m_vimPending.registerName;
    m_vimRegisterInUse = '\0';
    m_vimPending.registerName = '\0';
    return name;
}

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
    VimRegister::write(vimTakeRegister(), payload, linewise);
}

void EditorViewport::vimDeleteRange(size_t start, size_t end, bool linewise) {
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    vimSetRegister(removed, linewise);
    beginUndoStep();
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    endUndoStep();
    refreshCache();
    ensureCursorVisible();
    update();
}

/* Delete-then-insert as one undo step, leaving the registers alone:
 * `~`, `>>` and Ctrl+A rewrite text without yanking it. */
void EditorViewport::vimReplaceRange(size_t start, size_t end, const QByteArray &text) {
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    beginUndoStep();
    if (end > start && ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    if (!text.isEmpty() &&
        ase_buffer_insert(m_buffer, start, text.constData(), static_cast<size_t>(text.size()))) {
        ase_undo_record_insert(m_undo, start, text.constData(), static_cast<size_t>(text.size()));
    }
    endUndoStep();
    refreshCache();
}

/* ASCII only, like vim's default: a byte outside it is left as it is
 * rather than guessed at. */
void EditorViewport::vimToggleCaseInPlace(QByteArray &text) {
    for (char &ch : text) {
        unsigned char u = static_cast<unsigned char>(ch);
        if (u >= 'a' && u <= 'z') {
            ch = static_cast<char>(u - 'a' + 'A');
        } else if (u >= 'A' && u <= 'Z') {
            ch = static_cast<char>(u - 'A' + 'a');
        }
    }
}

void EditorViewport::vimToggleCase(int count) {
    size_t pos = m_cursors[0];
    size_t lineEnd = vimLineEndOffset(lineForOffset(pos));
    size_t end = pos;
    for (int n = 0; n < count && end < lineEnd; ++n) {
        end = vimNextCharBoundary(end);
    }
    if (end <= pos) {
        return;
    }

    QByteArray text = m_cache.mid(static_cast<int>(pos), static_cast<int>(end - pos));
    vimToggleCaseInPlace(text);

    vimReplaceRange(pos, end, text);
    /* One past the last byte touched, clamped back on to the line —
     * `~` on the final character leaves the cursor there. */
    lineEnd = vimLineEndOffset(lineForOffset(pos));
    m_cursors[0] = std::min(end, lineEnd > pos ? vimPrevCharBoundary(lineEnd) : pos);
    m_selectionAnchors[0] = m_cursors[0];
    vimMarkChange();
    ensureCursorVisible();
    update();
}

/* Four spaces, matching what Tab inserts — the render path has no
 * tab-stop expansion, so a literal tab would measure ~0 wide. See
 * docs/adr/0031. */
void EditorViewport::vimIndentLines(int startLine, int lineCount, bool right) {
    static const int kIndent = 4;
    int lastLine = std::min(startLine + lineCount - 1, vimLastLine());
    if (startLine > lastLine) {
        return;
    }

    beginUndoSession();
    /* Bottom-up: every edit shifts the offsets of the lines below it. */
    for (int line = lastLine; line >= startLine; --line) {
        size_t lineStart = static_cast<size_t>(m_lineStarts[line]);
        size_t lineEnd = vimLineEndOffset(line);
        if (right) {
            /* vim leaves an empty line empty rather than indenting it. */
            if (lineEnd > lineStart) {
                vimReplaceRange(lineStart, lineStart, QByteArray(kIndent, ' '));
            }
        } else {
            size_t scan = lineStart;
            while (scan < lineEnd && scan - lineStart < static_cast<size_t>(kIndent) &&
                   m_cache[static_cast<int>(scan)] == ' ') {
                scan++;
            }
            if (scan > lineStart) {
                vimReplaceRange(lineStart, scan, QByteArray());
            }
        }
    }
    endUndoSession();

    m_cursors[0] = vimFirstNonBlank(std::min(startLine, vimLastLine()));
    m_selectionAnchors[0] = m_cursors[0];
    vimMarkChange();
    ensureCursorVisible();
    update();
}

/* The first number at or after the cursor on this line, as vim scans:
 * a run of digits, taking a `-` immediately before it as a sign. A `.`
 * is not part of it, so 1.9 increments to 2.9. */
void EditorViewport::vimAddToNumber(int delta) {
    size_t pos = m_cursors[0];
    int line = lineForOffset(pos);
    size_t lineStart = static_cast<size_t>(m_lineStarts[line]);
    size_t lineEnd = vimLineEndOffset(line);

    size_t scan = pos;
    while (scan < lineEnd && !isdigit(static_cast<unsigned char>(m_cache[static_cast<int>(scan)]))) {
        scan++;
    }
    if (scan >= lineEnd) {
        return;
    }

    size_t start = scan;
    while (start > lineStart && isdigit(static_cast<unsigned char>(m_cache[static_cast<int>(start) - 1]))) {
        start--;
    }
    size_t end = scan;
    while (end < lineEnd && isdigit(static_cast<unsigned char>(m_cache[static_cast<int>(end)]))) {
        end++;
    }
    bool negative = start > lineStart && m_cache[static_cast<int>(start) - 1] == '-';
    if (negative) {
        start--;
    }

    long long value = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start)).toLongLong();
    QByteArray text = QByteArray::number(value + delta);
    vimReplaceRange(start, end, text);

    /* On the last digit of the result, where vim leaves it. */
    size_t landing = start + static_cast<size_t>(text.size());
    m_cursors[0] = (landing > start) ? vimPrevCharBoundary(landing) : start;
    m_selectionAnchors[0] = m_cursors[0];
    vimMarkChange();
    ensureCursorVisible();
    update();
}

/* A linewise delete has to take a newline with the line, and when the
 * range runs to the end of the buffer there may be none inside it to
 * take — either because the range is empty (the position past a
 * trailing newline, which is not a line at all) or because the file
 * does not end in a newline. Both cases take the newline *before* the
 * range instead, which is the one that made the line. A last line that
 * has its own newline keeps it, and pulling back there would eat the
 * one belonging to the line above. */
size_t EditorViewport::vimLinewiseDeleteStart(size_t start, size_t end) const {
    if (start == 0 || end < static_cast<size_t>(m_cache.size())) {
        return start;
    }
    bool nothingToTake = (start == end) || m_cache.back() != '\n';
    if (!nothingToTake) {
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
    beginUndoStep();
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    endUndoStep();
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

/* The half of `p` and `P` that does not depend on which one it is:
 * insert, record one undo step, and land the cursor. Charwise leaves it
 * on the *last* character pasted, not the first — real vim's rule, and
 * the one that makes a second `p` continue the text rather than paste
 * into the middle of it. Linewise is the opposite: first non-blank of
 * the first pasted line. Reported by an external tester, who expected
 * "the end" for both; see docs/adr/0059. */
void EditorViewport::vimInsertPaste(size_t insertAt, const QByteArray &bytes, bool linewise) {
    beginUndoStep();
    if (ase_buffer_insert(m_buffer, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()));
    }
    endUndoStep();
    refreshCache();
    size_t target = linewise
                        ? vimFirstNonBlank(lineForOffset(insertAt))
                        : vimPrevCharBoundary(insertAt + static_cast<size_t>(bytes.size()));
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimPasteAfter() {
    const VimRegister &reg = VimRegister::read(vimTakeRegister());
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
            /* A buffer not ending in a newline has nothing to append
             * after ("a\nb" + p gave "a\nbb"), so the pasted line brings
             * its own leading newline instead — and brings no trailing
             * one, because vim preserves the file's missing final
             * newline rather than adding one behind your back. */
            if (m_cache.isEmpty() || m_cache.back() == '\n') {
                if (!bytes.endsWith('\n')) {
                    bytes.append('\n');
                }
            } else {
                if (bytes.endsWith('\n')) {
                    bytes.chop(1);
                }
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
    vimInsertPaste(insertAt, bytes, linewise);
}

void EditorViewport::vimPasteBefore() {
    const VimRegister &reg = VimRegister::read(vimTakeRegister());
    if (reg.isEmpty()) {
        return;
    }
    QByteArray bytes = reg.text();
    bool linewise = reg.isLinewise();
    size_t insertAt = linewise ? static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])
                               : m_cursors[0];
    vimInsertPaste(insertAt, bytes, linewise);
}

QByteArray EditorViewport::indentOfLine(int line) const {
    if (!m_autoIndent || line < 0 || line >= m_lineStarts.size()) {
        return QByteArray();
    }
    int start = m_lineStarts[line];
    int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1
                                                : static_cast<int>(m_cache.size());
    int scan = start;
    while (scan < end && (m_cache[scan] == ' ' || m_cache[scan] == '\t')) {
        scan++;
    }
    return m_cache.mid(start, scan - start);
}

void EditorViewport::insertNewlineWithIndent() {
    const int line = lineForOffset(m_cursors[0]);
    const QByteArray indent = indentOfLine(line);
    if (indent.isEmpty()) {
        insertText(QByteArrayLiteral("\n"));
        return;
    }

    /*
     * The indent replaces whatever whitespace the split would otherwise
     * have pushed onto the new line, rather than adding to it.
     *
     * Splitting `    hello| world` gives `    world` in vim, not
     * `     world` — derived, not assumed, and the difference only
     * shows when the break lands on a space. See docs/adr/0136.
     */
    const int lineEnd = (line + 1 < m_lineStarts.size())
                            ? m_lineStarts[line + 1] - 1
                            : static_cast<int>(m_cache.size());
    size_t scan = m_cursors[0];
    while (scan < static_cast<size_t>(lineEnd) &&
           (m_cache[static_cast<int>(scan)] == ' ' || m_cache[static_cast<int>(scan)] == '\t')) {
        scan++;
    }

    const size_t at = m_cursors[0];
    vimReplaceRange(at, scan, QByteArrayLiteral("\n") + indent);
    const size_t landed = at + 1 + static_cast<size_t>(indent.size());
    collapseToOneCursor();
    m_cursors[0] = std::min(landed, static_cast<size_t>(m_cache.size()));
    m_selectionAnchors[0] = m_cursors[0];
    /* Remembered so Escape can take it back if nothing was typed on it,
     * which is what vim does. */
    noteAutoIndentedLine(lineForOffset(m_cursors[0]));
    ensureCursorVisible();
    update();
}

/*
 * vim leaves no trailing whitespace behind an indent you never used:
 * `o<Esc>` on an indented line leaves the line empty, not four spaces
 * wide. Derived from real vim rather than assumed — see docs/adr/0136.
 */
void EditorViewport::noteAutoIndentedLine(int line) {
    if (m_autoIndentFirst < 0) {
        m_autoIndentFirst = line;
    }
    m_autoIndentFirst = std::min(m_autoIndentFirst, line);
    m_autoIndentLast = std::max(m_autoIndentLast, line);
}

void EditorViewport::dropUnusedAutoIndent() {
    if (m_autoIndentFirst < 0) {
        return;
    }
    const int first = m_autoIndentFirst;
    const int last = m_autoIndentLast;
    m_autoIndentFirst = -1;
    m_autoIndentLast = -1;

    /* Bottom-up, because removing one line's indent shifts the offsets
     * of every line after it. */
    bool moved = false;
    for (int line = std::min(last, static_cast<int>(m_lineStarts.size()) - 1); line >= first;
         --line) {
        if (line < 0) {
            break;
        }
        int start = m_lineStarts[line];
        int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1
                                                    : static_cast<int>(m_cache.size());
        if (end <= start) {
            continue;
        }
        bool blank = true;
        for (int i = start; i < end; ++i) {
            if (m_cache[i] != ' ' && m_cache[i] != '\t') {
                blank = false; /* something real was typed; the indent is earned */
                break;
            }
        }
        if (!blank) {
            continue;
        }
        vimReplaceRange(static_cast<size_t>(start), static_cast<size_t>(end), QByteArray());
        /* The cursor may have been inside what just went. Left where it
         * was it clamps to the end of the buffer, and the next `o`
         * opens after the wrong line. */
        collapseToOneCursor();
        m_cursors[0] = static_cast<size_t>(start);
        m_selectionAnchors[0] = m_cursors[0];
        moved = true;
    }
    if (moved) {
        m_selectionAnchors[0] = m_cursors[0];
    }
}

/*
 * `3iab` types "ababab"; `3oab` opens three lines each holding "ab".
 * The captured insert is replayed verbatim, which is what makes
 * `2ia<CR>b` come out as "a\nba\nb" — vim repeats the bytes, not the
 * keystrokes. See docs/adr/0137.
 */
void EditorViewport::vimRepeatInsertForCount(const QByteArray &typed) {
    if (m_insertCount <= 1 || m_insertRepeating) {
        m_insertCount = 1;
        return;
    }
    const int count = m_insertCount;
    const bool opensLine = m_insertOpensLine;
    m_insertCount = 1;
    if (typed.isEmpty() && !opensLine) {
        return; /* nothing to repeat, and no line to open */
    }

    m_insertRepeating = true;
    for (int n = 1; n < count; ++n) {
        if (opensLine) {
            insertNewlineWithIndent();
        }
        if (!typed.isEmpty()) {
            insertText(typed);
        }
    }
    m_insertRepeating = false;
}

void EditorViewport::vimOpenLineAbove() {
    int line = lineForOffset(m_cursors[0]);
    size_t at = static_cast<size_t>(m_lineStarts[line]);
    /* Taken before the insert, from the line this one is going above. */
    const QByteArray indent = indentOfLine(line);
    const QByteArray inserted = indent + QByteArrayLiteral("\n");
    beginUndoStep();
    if (ase_buffer_insert(m_buffer, at, inserted.constData(),
                           static_cast<size_t>(inserted.size()))) {
        ase_undo_record_insert(m_undo, at, inserted.constData(),
                                static_cast<size_t>(inserted.size()));
    }
    endUndoStep();
    at += static_cast<size_t>(indent.size());
    m_cursors[0] = at;
    m_selectionAnchors[0] = at;
    if (!indent.isEmpty()) {
        noteAutoIndentedLine(line);
    }
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
/* Recorded before dispatch, so a macro replays the keys as typed. The
 * `q` that ends recording is dropped by vimStopRecordingMacro. */
void EditorViewport::vimRecordMacroKey(QKeyEvent *event) {
    if (m_macroRecording == '\0' || m_macroReplayDepth > 0) {
        return;
    }
    RecordedKey recorded;
    recorded.key = event->key();
    recorded.mods = event->modifiers();
    recorded.text = event->text();
    m_macroBuffer.push_back(recorded);
}

void EditorViewport::vimStopRecordingMacro() {
    if (m_macroRecording == '\0') {
        return;
    }
    /* The `q` that stopped it was recorded a moment ago; replaying it
     * would start a recording inside the macro. */
    if (!m_macroBuffer.isEmpty()) {
        m_macroBuffer.removeLast();
    }
    m_macros.insert(m_macroRecording, m_macroBuffer);
    notify(NotifyLevel::Info, QStringLiteral("recorded @%1 (%2 keys)")
                                   .arg(QChar(m_macroRecording))
                                   .arg(m_macroBuffer.size()));
    m_macroRecording = '\0';
    m_macroBuffer.clear();
}

void EditorViewport::vimPlayMacro(char name, int count) {
    auto it = m_macros.constFind(name);
    if (it == m_macros.constEnd() || it->isEmpty()) {
        notify(NotifyLevel::Warning, QStringLiteral("register %1 is empty").arg(QChar(name)));
        return;
    }
    m_macroLastPlayed = name;

    /* A macro may call itself; the budget is what stops one that never
     * stops. Set once per outermost play so nesting shares it. */
    if (m_macroReplayDepth == 0) {
        m_macroReplayBudget = 200000;
    }
    if (m_macroReplayDepth > 32) {
        /* Silently unwinding would look like the macro simply did less
         * than it was asked to. */
        notify(NotifyLevel::Warning, QStringLiteral("@%1 stopped: nested too deep").arg(QChar(name)));
        return;
    }

    /* Copied: replaying may re-record this register, and iterating a
     * container being written is how that ends badly. */
    QVector<RecordedKey> keys = *it;
    m_macroReplayDepth++;
    for (int pass = 0; pass < count; ++pass) {
        for (const RecordedKey &recorded : keys) {
            if (m_macroReplayBudget-- <= 0) {
                notify(NotifyLevel::Warning, QStringLiteral("@%1 stopped: too many keys")
                                                   .arg(QChar(name)));
                m_macroReplayDepth--;
                return;
            }
            QKeyEvent replay(QEvent::KeyPress, recorded.key, recorded.mods, recorded.text);
            keyPressEvent(&replay);
        }
    }
    m_macroReplayDepth--;
}

/* (line, column), the same shape the jumplist stores, so an edit above
 * a mark leaves it pointing at the old line number — see docs/adr/0097. */
void EditorViewport::vimSetMark(char name) {
    m_vimMarks.insert(name, qMakePair(cursorLine(), cursorColumn()));
    /* Kept locally too, so an operator can use it while this is the
     * buffer it lives in — vim errors on one in another file. */
    if (name >= 'A' && name <= 'Z') {
        emit globalMarkSetRequested(name);
    }
}

void EditorViewport::vimApplyOperatorToMark(char name, bool exact) {
    auto it = m_vimMarks.constFind(name);
    if (it == m_vimMarks.constEnd()) {
        notify(NotifyLevel::Warning, QStringLiteral("mark %1 not set").arg(QChar(name)));
        resetVimPendingState();
        return;
    }
    int lineCount = std::max(1, static_cast<int>(m_lineStarts.size()));
    int markLine = std::clamp(it->first, 1, lineCount);

    if (!exact) {
        /* `'` is linewise and inclusive of both ends. */
        int cursorLine = lineForOffset(m_cursors[0]);
        int first = std::min(cursorLine, markLine - 1);
        int last = std::max(cursorLine, markLine - 1);
        vimApplyPendingOperatorLinewise(first, last - first + 1);
        return;
    }

    /* `` ` `` is charwise and exclusive: the byte under the later of the
     * two positions survives. */
    size_t markOffset = offsetForLineColumn(markLine - 1, it->second - 1);
    size_t cursor = m_cursors[0];
    vimApplyPendingOperatorCharwise(std::min(cursor, markOffset), std::max(cursor, markOffset));
}

void EditorViewport::vimJumpToMark(char name, bool exact) {
    /* Always via the window: it knows which buffer the mark is in, and
     * the answer may be one that is not even open. */
    if (name >= 'A' && name <= 'Z') {
        emit globalMarkJumpRequested(name, exact);
        return;
    }
    auto it = m_vimMarks.constFind(name);
    if (it == m_vimMarks.constEnd()) {
        notify(NotifyLevel::Warning, QStringLiteral("mark %1 not set").arg(QChar(name)));
        return;
    }

    /* A mark jump is a jump: Ctrl+O comes back from it. */
    recordJump();

    int line = std::clamp(it->first, 1, std::max(1, static_cast<int>(m_lineStarts.size())));
    if (exact) {
        goToLineColumn(line, it->second);
        return;
    }
    collapseToOneCursor();
    size_t target = vimFirstNonBlank(line - 1);
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    resetVimPendingState();
    ensureCursorVisible();
    resetCaretBlink();
    update();
}

void EditorViewport::vimRecordKey(QChar qc) {
    /* Vim's `showcmd`, in the status bar: what has been typed so far
     * for a command that has not resolved. `2d` sits there until the
     * motion arrives. Without it a half-typed operator is invisible and
     * the next keystroke appears to do something arbitrary. See
     * docs/adr/0121. */
    if (!m_dotReplaying) {
        if (m_vimPending.idle() && m_vimPendingKeys.isEmpty()) {
            m_vimPendingKeys.clear();
        }
        m_vimPendingKeys.append(qc);
        emit pendingInputChanged(m_vimPendingKeys);
    }
    if (m_dotReplaying) {
        return;
    }
    /* A key with nothing pending begins a new command, so whatever was
     * being recorded came to nothing and is dropped. */
    if (m_vimMode != VimMode::Visual && m_vimPending.idle()) {
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

/* Entering Insert from a Normal-mode command, remembering the count so
 * leaving can repeat what was typed. See docs/adr/0137. */
void EditorViewport::vimBeginInsert(int count, bool opensLine) {
    m_vimMode = VimMode::Insert;
    m_insertCount = m_insertRepeating ? 1 : std::max(1, count);
    m_insertOpensLine = opensLine;
    vimMarkChange();
    vimBeginInsertCapture();
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
        /* The replayed keys carried the original count, and the text
         * came from m_dotInserted rather than through the capture
         * buffer, so the repeat has to be handed it explicitly. */
        vimRepeatInsertForCount(m_dotInserted);
        dropUnusedAutoIndent();
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

    beginUndoStep();
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
    endUndoStep();
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
size_t EditorViewport::vimVisualEnd(int i) const {
    /* Linewise: through the end of the last selected line, including
     * its newline, worked out from the line numbers rather than from
     * where the cursor happens to be. The cursor sits *on* the last
     * line, not past it — see vimNormalizeLinewiseSelection(). */
    if (m_vimMode == VimMode::Visual && m_vimVisualLinewise) {
        int lineCount = static_cast<int>(m_lineStarts.size());
        int last = std::clamp(std::max(m_vimVisualAnchorLine, m_vimVisualCursorLine), 0,
                               std::max(0, lineCount - 1));
        return (last + 1 < lineCount) ? static_cast<size_t>(m_lineStarts[last + 1])
                                       : static_cast<size_t>(m_cache.size());
    }
    size_t end = selectionMaxAt(i);
    if (m_vimMode != VimMode::Visual) {
        return end;
    }
    /* One character further, but never over the line break: a charwise
     * selection sitting on the last character of a line covers that
     * character and stops there. */
    int line = lineForOffset(end);
    size_t lineEnd = (line + 1 < m_lineStarts.size())
                         ? static_cast<size_t>(m_lineStarts[line + 1] - 1)
                         : static_cast<size_t>(m_cache.size());
    if (end >= lineEnd) {
        return end;
    }
    return vimNextCharBoundary(end);
}

void EditorViewport::vimReplaceSelection(QChar target) {
    size_t start = selectionMinAt(0);
    size_t end = vimVisualEnd(0);
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

    beginUndoStep();
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
    endUndoStep();
    refreshCache();
    vimMarkChange();
}

/* Change [start, end), or just enter Insert when there is nothing there
 * — `s` and `C` on an empty line still start typing. */
void EditorViewport::vimChangeOrInsert(size_t start, size_t end) {
    if (end > start) {
        m_vimPending.op = 'c';
        vimApplyPendingOperatorCharwise(start, end);
        return;
    }
    beginUndoSession();
    m_vimMode = VimMode::Insert;
    vimMarkChange();
    vimBeginInsertCapture();
}

/*
 * Vim does not require the cursor to be on a bracket: it scans forward
 * along the line for the first one and matches that, which is what makes
 * `%` usable from the start of a line of code.
 *
 * Nesting is counted, and the search for the partner runs over the whole
 * buffer rather than the line, since that is the common case. Brackets
 * inside strings and comments are not skipped — plain vim does not skip
 * them either.
 */
namespace {
bool isBlankByte(char c) { return c == ' ' || c == '\t'; }
} // namespace

/* Enclosing first — being on either bracket counts as inside — then the
 * next pair opening later on this line, which is what vim does when the
 * cursor sits before one. */
bool EditorViewport::vimEnclosingPair(size_t pos, char open, char close, size_t *outOpen,
                                       size_t *outClose) const {
    int len = m_cache.size();
    if (len == 0) {
        return false;
    }
    int at = static_cast<int>(std::min(pos, static_cast<size_t>(len - 1)));

    int openAt = -1;
    if (m_cache[at] == open) {
        openAt = at;
    } else {
        /* Backwards for an unmatched opener; a closer on the way means a
         * complete pair that does not contain us. */
        int depth = 0;
        for (int i = (m_cache[at] == close) ? at - 1 : at; i >= 0; --i) {
            if (m_cache[i] == close) {
                depth++;
            } else if (m_cache[i] == open) {
                if (depth == 0) {
                    openAt = i;
                    break;
                }
                depth--;
            }
        }
    }

    if (openAt < 0) {
        int lineEnd = static_cast<int>(vimLineEndOffset(lineForOffset(pos)));
        for (int i = at; i < lineEnd; ++i) {
            if (m_cache[i] == open) {
                openAt = i;
                break;
            }
        }
    }
    if (openAt < 0) {
        return false;
    }

    int depth = 0;
    for (int i = openAt; i < len; ++i) {
        if (m_cache[i] == open) {
            depth++;
        } else if (m_cache[i] == close && --depth == 0) {
            *outOpen = static_cast<size_t>(openAt);
            *outClose = static_cast<size_t>(i);
            return true;
        }
    }
    return false;
}

/* Quotes do not nest, so the pairs on a line are simply every other one.
 * The pair containing the cursor wins; failing that, the next to open. */
bool EditorViewport::vimQuotedRange(size_t pos, char quote, size_t *outOpen,
                                     size_t *outClose) const {
    int line = lineForOffset(pos);
    int from = m_lineStarts[line];
    int to = static_cast<int>(vimLineEndOffset(line));

    for (int i = from; i < to; ++i) {
        if (m_cache[i] != quote || (i > from && m_cache[i - 1] == '\\')) {
            continue;
        }
        for (int j = i + 1; j < to; ++j) {
            if (m_cache[j] != quote || m_cache[j - 1] == '\\') {
                continue;
            }
            if (static_cast<size_t>(j) >= pos) {
                *outOpen = static_cast<size_t>(i);
                *outClose = static_cast<size_t>(j);
                return true;
            }
            i = j; /* a closed pair entirely before the cursor */
            break;
        }
        if (static_cast<size_t>(i) > pos) {
            break; /* an unterminated quote past the cursor */
        }
    }
    return false;
}

/* vim inserts one space between the lines, except where it would be
 * wrong: the first line already ends in blank, either line is empty, or
 * the next begins with `)`. `gJ` skips the rule and the indent strip. */
void EditorViewport::vimJoinLines(int count, bool withSpace) {
    int joins = std::max(2, count) - 1;
    size_t landing = m_cursors[0];
    bool any = false;

    beginUndoSession();
    for (int n = 0; n < joins; ++n) {
        int line = lineForOffset(landing);
        if (line + 1 > vimLastLine()) {
            /* vimLastLine, not m_lineStarts.size(): the position past a
             * trailing newline is not a line to join with, and treating
             * it as one ate the newline. */
            break;
        }

        size_t lineStart = static_cast<size_t>(m_lineStarts[line]);
        size_t lineEnd = vimLineEndOffset(line);
        size_t contentStart = static_cast<size_t>(m_lineStarts[line + 1]);
        size_t nextEnd = vimLineEndOffset(line + 1);
        if (withSpace) {
            while (contentStart < nextEnd && isBlankByte(m_cache[static_cast<int>(contentStart)])) {
                contentStart++;
            }
        }

        QByteArray separator;
        if (withSpace && lineEnd > lineStart && contentStart < nextEnd &&
            !isBlankByte(m_cache[static_cast<int>(lineEnd) - 1]) &&
            m_cache[static_cast<int>(contentStart)] != ')') {
            separator = QByteArrayLiteral(" ");
        }

        QByteArray removed = m_cache.mid(static_cast<int>(lineEnd),
                                          static_cast<int>(contentStart - lineEnd));
        beginUndoStep();
        if (ase_buffer_delete(m_buffer, lineEnd, contentStart - lineEnd)) {
            ase_undo_record_delete(m_undo, lineEnd, removed.constData(),
                                    static_cast<size_t>(removed.size()));
        }
        if (!separator.isEmpty() &&
            ase_buffer_insert(m_buffer, lineEnd, separator.constData(),
                               static_cast<size_t>(separator.size()))) {
            ase_undo_record_insert(m_undo, lineEnd, separator.constData(),
                                    static_cast<size_t>(separator.size()));
        }
        endUndoStep();
        refreshCache();

        /* Where the lines met, which is what vim leaves under the
         * cursor — the inserted space, or the next line's first byte. */
        landing = lineEnd;
        any = true;
    }
    endUndoSession();

    if (!any) {
        return;
    }
    landing = std::min(landing, static_cast<size_t>(m_cache.size()));
    collapseToOneCursor();
    m_cursors[0] = landing;
    m_selectionAnchors[0] = landing;
    vimMarkChange();
    ensureCursorVisible();
    update();
}

void EditorViewport::vimApplyTextObject(char kind, char object) {
    VimObjectRange range = vimTextObjectRange(kind, object);
    if (!range.valid || range.end <= range.start) {
        /* Nothing of that shape here; vim leaves the buffer alone. */
        resetVimPendingState();
        return;
    }

    /* A change into a whole-line block keeps the closing newline so
     * there is a line to type on — the same split `C` has from `D`. */
    if (m_vimPending.op == 'c' && range.end > range.start &&
        m_cache[static_cast<int>(range.end) - 1] == '\n' &&
        range.start == static_cast<size_t>(m_lineStarts[lineForOffset(range.start)])) {
        range.end--;
    }

    if (m_vimMode == VimMode::Visual) {
        /* Selects it rather than operating, so `viw` then `d` works and
         * so does `viwc`. */
        collapseToOneCursor();
        m_selectionAnchors[0] = range.start;
        m_cursors[0] = range.end - 1;
        m_vimVisualLinewise = false;
        resetVimPendingState();
        return;
    }
    vimApplyPendingOperatorCharwise(range.start, range.end);
}

EditorViewport::VimObjectRange EditorViewport::vimTextObjectRange(char kind, char object) const {
    VimObjectRange result;
    int len = m_cache.size();
    if (len == 0) {
        return result;
    }
    size_t pos = std::min(m_cursors[0], static_cast<size_t>(len - 1));
    bool around = (kind == 'a');

    if (object == 'w' || object == 'W') {
        /* A WORD is a run of non-blanks; a word is a run of one class. */
        auto sameRun = [&](size_t a, size_t b) {
            if (object == 'W') {
                return isBlankByte(m_cache[static_cast<int>(a)]) ==
                       isBlankByte(m_cache[static_cast<int>(b)]);
            }
            return vimClassifyAt(a) == vimClassifyAt(b);
        };
        size_t start = pos;
        while (start > 0 && m_cache[static_cast<int>(start) - 1] != '\n' && sameRun(start - 1, pos)) {
            start--;
        }
        size_t end = pos;
        while (end + 1 < static_cast<size_t>(len) && m_cache[static_cast<int>(end)] != '\n' &&
               sameRun(end + 1, pos)) {
            end++;
        }
        end++; /* exclusive */

        if (around) {
            /* The trailing blanks, or the leading ones when there are
             * none — vim's rule, and what makes `daw` on a line's last
             * word take the space before it. */
            size_t after = end;
            while (after < static_cast<size_t>(len) && isBlankByte(m_cache[static_cast<int>(after)])) {
                after++;
            }
            if (after > end) {
                end = after;
            } else {
                while (start > 0 && isBlankByte(m_cache[static_cast<int>(start) - 1])) {
                    start--;
                }
            }
        }
        result.start = start;
        result.end = end;
        result.valid = end > start;
        return result;
    }

    char open = '\0';
    char close = '\0';
    switch (object) {
    case '(': case ')': case 'b': open = '('; close = ')'; break;
    case '{': case '}': case 'B': open = '{'; close = '}'; break;
    case '[': case ']': open = '['; close = ']'; break;
    case '<': case '>': open = '<'; close = '>'; break;
    default: break;
    }

    if (open != '\0') {
        size_t o = 0;
        size_t c = 0;
        if (!vimEnclosingPair(pos, open, close, &o, &c)) {
            return result;
        }

        int openLine = lineForOffset(o);
        int closeLine = lineForOffset(c);
        if (!around && closeLine > openLine) {
            /* vim: an inner block whose braces sit alone at the ends of
             * their lines covers whole lines, so `di{` on a function body
             * leaves the braces where they are. */
            bool tailBlank = true;
            for (size_t i = o + 1; i < vimLineEndOffset(openLine); ++i) {
                if (!isBlankByte(m_cache[static_cast<int>(i)])) {
                    tailBlank = false;
                    break;
                }
            }
            bool headBlank = true;
            for (size_t i = static_cast<size_t>(m_lineStarts[closeLine]); i < c; ++i) {
                if (!isBlankByte(m_cache[static_cast<int>(i)])) {
                    headBlank = false;
                    break;
                }
            }
            if (tailBlank && headBlank) {
                result.start = static_cast<size_t>(m_lineStarts[openLine + 1]);
                result.end = static_cast<size_t>(m_lineStarts[closeLine]);
                result.valid = result.end > result.start;
                return result;
            }
        }

        result.start = around ? o : o + 1;
        result.end = around ? c + 1 : c;
        result.valid = result.end >= result.start;
        return result;
    }

    if (object == '"' || object == '\'' || object == '`') {
        size_t o = 0;
        size_t c = 0;
        if (!vimQuotedRange(pos, object, &o, &c)) {
            return result;
        }
        result.start = around ? o : o + 1;
        result.end = around ? c + 1 : c;
        if (around) {
            /* `a"` takes the blanks after the closing quote, or before
             * the opening one when there are none. */
            size_t after = result.end;
            while (after < static_cast<size_t>(len) && isBlankByte(m_cache[static_cast<int>(after)])) {
                after++;
            }
            if (after > result.end) {
                result.end = after;
            } else {
                while (result.start > 0 &&
                       isBlankByte(m_cache[static_cast<int>(result.start) - 1])) {
                    result.start--;
                }
            }
        }
        result.valid = result.end >= result.start;
        return result;
    }

    return result;
}

size_t EditorViewport::vimMatchBracket(size_t pos) const {
    static const char kOpen[] = "([{";
    static const char kClose[] = ")]}";
    int len = m_cache.size();
    if (pos >= static_cast<size_t>(len)) {
        return pos;
    }
    int lineEnd = static_cast<int>(vimLineEndOffset(lineForOffset(pos)));

    int at = -1;
    int kind = -1;
    bool opening = false;
    for (int i = static_cast<int>(pos); i < lineEnd && at < 0; ++i) {
        for (int k = 0; k < 3; ++k) {
            if (m_cache[i] == kOpen[k]) {
                at = i; kind = k; opening = true; break;
            }
            if (m_cache[i] == kClose[k]) {
                at = i; kind = k; opening = false; break;
            }
        }
    }
    if (at < 0) {
        return pos;
    }

    char open = kOpen[kind];
    char close = kClose[kind];
    int depth = 0;
    if (opening) {
        for (int i = at; i < len; ++i) {
            if (m_cache[i] == open) {
                depth++;
            } else if (m_cache[i] == close && --depth == 0) {
                return static_cast<size_t>(i);
            }
        }
    } else {
        for (int i = at; i >= 0; --i) {
            if (m_cache[i] == close) {
                depth++;
            } else if (m_cache[i] == open && --depth == 0) {
                return static_cast<size_t>(i);
            }
        }
    }
    return pos; /* unmatched */
}

size_t EditorViewport::vimLineEndOffset(int line) const {
    return (line + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[line + 1] - 1)
                                            : static_cast<size_t>(m_cache.size());
}

void EditorViewport::vimEnterReplaceMode(int count) {
    m_vimMode = VimMode::Insert;
    m_vimReplacing = true;
    m_replaceOriginals.clear();
    m_replaceTyped.clear();
    m_replaceCount = std::max(1, count);
    vimMarkChange();
    beginUndoSession();
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

    beginUndoStep();
    if (overwrite && ase_buffer_delete(m_buffer, cursor, end - cursor)) {
        ase_undo_record_delete(m_undo, cursor, original.constData(), static_cast<size_t>(original.size()));
    }
    if (ase_buffer_insert(m_buffer, cursor, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, cursor, bytes.constData(), static_cast<size_t>(bytes.size()));
        cursor += static_cast<size_t>(bytes.size());
    }
    m_cursors[0] = cursor;
    m_selectionAnchors[0] = cursor;
    endUndoStep();

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

    beginUndoStep();
    if (ase_buffer_delete(m_buffer, start, cursor - start)) {
        ase_undo_record_delete(m_undo, start, typed.constData(), static_cast<size_t>(typed.size()));
    }
    if (!original.isEmpty() &&
        ase_buffer_insert(m_buffer, start, original.constData(), static_cast<size_t>(original.size()))) {
        ase_undo_record_insert(m_undo, start, original.constData(), static_cast<size_t>(original.size()));
    }
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    endUndoStep();

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
    if (m_vimPending.replace) {
        m_vimPending.replace = false;
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
            vimReplaceChar(qc, m_vimPending.count(), newline);
        }
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    /* This key is the target, whatever it is: `f3` and `fd` search for
     * '3' and 'd', not a count or an operator. */
    if (m_vimPending.find != '\0') {
        char command = m_vimPending.find;
        m_vimPending.find = '\0';
        char target = qc.toLatin1();
        if (target == '\0') {
            resetVimPendingState(); /* non-Latin1 target: nothing to search for */
            return true;
        }
        m_vimLastFindCommand = command;
        m_vimLastFindTarget = target;
        vimApplyFindInLine(command, target, m_vimPending.count());
        return true;
    }

    /* Mid-`"`: this key names the register. It is deliberately not
     * cleared here — the yank, delete or paste that follows consumes it. */
    if (m_vimPending.awaitingRegister) {
        m_vimPending.awaitingRegister = false;
        char name = qc.toLatin1();
        if ((name >= 'a' && name <= 'z') || (name >= 'A' && name <= 'Z')) {
            m_vimPending.registerName = name;
        }
        return true;
    }

    /* Mid-`i`/`a`: this key names the text object. */
    if (m_vimPending.textObject != '\0') {
        char kind = m_vimPending.textObject;
        m_vimPending.textObject = '\0';
        char object = qc.toLatin1();
        if (object != '\0') {
            vimApplyTextObject(kind, object);
        }
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    /* Mid-`q` or `@`: this key names the register. */
    if (m_vimPending.macro != '\0') {
        char pending = m_vimPending.macro;
        m_vimPending.macro = '\0';
        int count = m_vimPending.count();
        char name = qc.toLatin1();
        if (pending == '@' && name == '@') {
            name = m_macroLastPlayed; /* `@@` repeats the last one played */
        }
        resetVimPendingState();
        if (name != '\0' && (qc.isLetterOrNumber() || pending == '@')) {
            if (pending == 'q') {
                m_macroRecording = name;
                m_macroBuffer.clear();
                notify(NotifyLevel::Info, QStringLiteral("recording @%1").arg(QChar(name)));
            } else {
                vimPlayMacro(name, count);
            }
        }
        ensureCursorVisible();
        update();
        return true;
    }

    /* Mid-`m`, `` ` `` or `'`: this key names the mark, whatever it is. */
    if (m_vimPending.mark != '\0') {
        char pending = m_vimPending.mark;
        m_vimPending.mark = '\0';
        char name = qc.toLatin1();
        if (name != '\0' && (qc.isLetter() || pending != 'm')) {
            if (pending == 'm') {
                vimSetMark(name);
            } else if (m_vimPending.op != '\0') {
                vimApplyOperatorToMark(name, pending == '`');
            } else {
                vimJumpToMark(name, pending == '`');
            }
        }
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    /* Mid-"g": resolved on the very next key, whatever it is. */
    if (m_vimPending.g) {
        m_vimPending.g = false;
        if (qc == QLatin1Char('g')) {
            int targetLine = (m_vimPending.count1 > 0) ? std::min(m_vimPending.count1 - 1, vimLastLine()) : 0;
            if (m_vimPending.op == '\0') {
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
        } else if (qc == QLatin1Char('J')) {
            int count = m_vimPending.count();
            resetVimPendingState();
            vimJoinLines(count, false);
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
        int &count = (m_vimPending.op != '\0') ? m_vimPending.count2 : m_vimPending.count1;
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
    if (c == '*' || c == '#') {
        vimSearchWordUnderCursor(c == '*');
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
        m_vimPending.find = c;
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
        m_vimPending.g = true;
        return true;
    }
    if (c == 'G') {
        int targetLine = (m_vimPending.count1 > 0) ? std::min(m_vimPending.count1 - 1, vimLastLine()) : vimLastLine();
        if (m_vimPending.op == '\0') {
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
        c == 'W' || c == 'E' || c == 'B' ||
        c == 'b' || c == 'e' || c == '{' || c == '}' || c == '%') {
        if ((c == '{' || c == '}') && m_vimPending.op == '\0') {
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
    case '>':
    case '<': {
        /* Always whole lines, charwise selection or not. */
        int startLine = lineForOffset(selectionMinAt(0));
        int endLine = lineForOffset(vimVisualEnd(0) > selectionMinAt(0) ? vimVisualEnd(0) - 1
                                                                       : selectionMinAt(0));
        collapseToOneCursor();
        m_vimMode = VimMode::Normal;
        m_vimVisualLinewise = false;
        vimIndentLines(startLine, endLine - startLine + 1, c == '>');
        break;
    }
    case '~': {
        size_t start = selectionMinAt(0);
        size_t end = vimVisualEnd(0);
        if (end > start) {
            QByteArray text = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
            vimToggleCaseInPlace(text);
            vimReplaceRange(start, end, text);
            vimMarkChange();
        }
        collapseToOneCursor();
        m_cursors[0] = start;
        m_selectionAnchors[0] = start;
        m_vimMode = VimMode::Normal;
        m_vimVisualLinewise = false;
        break;
    }
    case 'x':
    case 'd': {
        size_t start = selectionMinAt(0);
        size_t end = vimVisualEnd(0);
        /* Not hasSelectionAt(): with an inclusive range, anchor ==
         * cursor still means one character is selected. */
        if (end > start) {
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
    }
    case 'y':
        if (vimVisualEnd(0) > selectionMinAt(0)) {
            /* p/P need to know which kind this was. */
            vimYankRange(selectionMinAt(0), vimVisualEnd(0), m_vimVisualLinewise);
        }
        collapseToOneCursor();
        m_vimMode = VimMode::Normal;
        m_vimVisualLinewise = false;
        break;
    case 'c':
        if (vimVisualEnd(0) > selectionMinAt(0)) {
            /* Not extended over the preceding newline as `d` is, or
             * the insertion point lands on the previous line. */
            vimChangeRange(selectionMinAt(0), vimVisualEnd(0), m_vimVisualLinewise);
        } else {
            m_vimMode = VimMode::Insert;
        }
        m_vimVisualLinewise = false;
        break;
    case 'r':
        /* Early: the tail below would reset the pending flag before the
         * replacement character ever arrived. */
        m_vimPending.replace = true;
        return;
    case 'i':
    case 'a':
        /* In Visual these only ever introduce a text object — `viw`
         * selects the word rather than entering Insert. */
        m_vimPending.textObject = c;
        return;
    case '"':
        m_vimPending.awaitingRegister = true;
        return;
    case 'J': {
        /* Every line the selection touches, however it was made. */
        int first = lineForOffset(selectionMinAt(0));
        int last = lineForOffset(vimVisualEnd(0) > selectionMinAt(0) ? vimVisualEnd(0) - 1
                                                                     : selectionMinAt(0));
        collapseToOneCursor();
        m_cursors[0] = static_cast<size_t>(m_lineStarts[first]);
        m_selectionAnchors[0] = m_cursors[0];
        m_vimMode = VimMode::Normal;
        m_vimVisualLinewise = false;
        vimJoinLines(last - first + 1, true);
        return;
    }
    default:
        break; /* unrecognized in Visual: swallowed, no state change */
    }
    resetVimPendingState();
    ensureCursorVisible();
    update();
}

void EditorViewport::vimApplyNormalKey(char c, int count) {
    /* Normal-mode-only from here: operators, x/p/P/u, mode entry. */

    if (c == 'd' || c == 'y' || c == 'c' || c == '>' || c == '<') {
        if (m_vimPending.op == c) {
            /* `2dd` on the last line does nothing: there is no second
             * line to take, and vim fails the command rather than
             * quietly doing half of it. Off the last line the count
             * clamps instead. */
            int startLine = lineForOffset(m_cursors[0]);
            int lastLine = 0;
            if (!vimLineBelow(startLine, count, &lastLine)) {
                resetVimPendingState();
                ensureCursorVisible();
                update();
                return;
            }
            vimApplyPendingOperatorLinewise(startLine, lastLine - startLine + 1);
        } else if (m_vimPending.op == '\0') {
            m_vimPending.op = c;
        } else {
            resetVimPendingState();
        }
        ensureCursorVisible();
        update();
        return;
    }

    /* Before the guard below: a mark is a motion, so `d'a` has to keep
     * the pending operator rather than have it abandoned here. */
    if (c == '`' || c == '\'') {
        m_vimPending.mark = c;
        return;
    }

    /* Likewise `i`/`a`: with an operator waiting they introduce a text
     * object rather than entering Insert. */
    if (m_vimPending.op != '\0' && (c == 'i' || c == 'a')) {
        m_vimPending.textObject = c;
        return;
    }

    /* A register prefix is not a command: `"ayy` must keep whatever is
     * pending and wait for the letter. Unhandled, the `a` that follows
     * entered Insert and typed the rest of the command into the file. */
    if (c == '"') {
        m_vimPending.awaitingRegister = true;
        return;
    }

    if (m_vimPending.op != '\0') {
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
        beginUndoSession();
        vimBeginInsert(count, false);
        break;
    case 'a':
        beginUndoSession();
        moveCursorRightAt(0, false);
        vimBeginInsert(count, false);
        break;
    case 'I': {
        beginUndoSession();
        size_t target = vimFirstNonBlank(lineForOffset(m_cursors[0]));
        m_cursors[0] = target;
        m_selectionAnchors[0] = target;
        vimBeginInsert(count, false);
        break;
    }
    case 'A':
        beginUndoSession();
        moveCursorEndAt(0, false);
        vimBeginInsert(count, false);
        break;
    case 'o':
        /* Before the line break, or undo leaves the blank line behind. */
        beginUndoSession();
        moveCursorEndAt(0, false);
        insertNewlineWithIndent();
        vimBeginInsert(count, true);
        break;
    case 'O':
        beginUndoSession();
        vimOpenLineAbove();
        /* Each repeat opens below the one before it, which is what vim
         * does: 3O leaves the three new lines in the order typed. */
        vimBeginInsert(count, true);
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
    case 'q':
        /* While recording, `q` is the stop key and takes no register. */
        if (m_macroRecording != '\0') {
            vimStopRecordingMacro();
            break;
        }
        m_vimPending.macro = 'q';
        return;
    case '@':
        m_vimPending.macro = '@';
        return; /* the count belongs to the replay, so keep it */
    case 'm':
        /* Early, like `r`: the mark's name is the next key, and the tail
         * below would clear the pending flag before it arrived. */
        m_vimPending.mark = c;
        return;
    case 'J':
        vimJoinLines(count, true);
        break;
    case '~':
        vimToggleCase(count);
        break;
    case 'r':
        m_vimPending.replace = true;
        return; /* the count is still needed when the target arrives */
    case 'R':
        vimEnterReplaceMode(count);
        break;
    case 's': {
        /* `c` over `count` characters, never past the line end. */
        size_t start = m_cursors[0];
        size_t end = start;
        size_t lineEnd = vimLineEndOffset(lineForOffset(start));
        for (int n = 0; n < count && end < lineEnd; ++n) {
            end = vimNextCharBoundary(end);
        }
        vimChangeOrInsert(start, end);
        break;
    }
    case 'S': {
        /* `cc`, which the linewise operator already is. */
        int startLine = lineForOffset(m_cursors[0]);
        int lastLine = 0;
        if (!vimLineBelow(startLine, count, &lastLine)) {
            break;
        }
        m_vimPending.op = 'c';
        vimApplyPendingOperatorLinewise(startLine, lastLine - startLine + 1);
        break;
    }
    case 'C':
    case 'D': {
        /* A count runs to the end of the count-th line down, so `2D`
         * takes the rest of this line and all of the next. */
        size_t start = m_cursors[0];
        int lastLine = 0;
        if (!vimLineBelow(lineForOffset(start), count, &lastLine)) {
            break;
        }
        size_t end = vimLineEndOffset(lastLine);
        if (c == 'C') {
            /* `2C` collapses the lines into one to type on, so the
             * newline between them goes but the last one's stays. */
            vimChangeOrInsert(start, end);
            break;
        }
        /* A counted `D` that starts at column 1 empties this line as well
         * as taking the ones below, so the line itself goes rather than
         * being left blank. From any other column the line survives with
         * what was before the cursor, and keeps its newline. */
        size_t lineStart = static_cast<size_t>(m_lineStarts[lineForOffset(start)]);
        if (count > 1 && start == lineStart && end < static_cast<size_t>(m_cache.size())) {
            end++;
        }
        if (end > start) {
            m_vimPending.op = 'd';
            vimApplyPendingOperatorCharwise(start, end);
        }
        break;
    }
    case 'X': {
        /* The mirror of `x`: `count` characters before the cursor,
         * never past the start of the line. */
        size_t end = m_cursors[0];
        size_t start = end;
        size_t lineStart = static_cast<size_t>(m_lineStarts[lineForOffset(end)]);
        for (int n = 0; n < count && start > lineStart; ++n) {
            start = vimPrevCharBoundary(start);
        }
        if (start < end) {
            vimDeleteRange(start, end);
            vimMarkChange();
        }
        break;
    }
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
        /* The only two Normal-mode commands that are chords; everything
         * else with a modifier belongs to the editor, not to vim. This
         * shadows Ctrl+A (select all) and Ctrl+X (cut) in Normal mode
         * only, the way Ctrl+D/Ctrl+U already do — see docs/adr/0059.
         * Insert, Visual and vim_mode = false keep the editor's. */
        bool ctrlOnly = (event->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) == 0 &&
                        (event->modifiers() & Qt::ControlModifier) != 0;
        if (ctrlOnly && m_vimMode == VimMode::Normal &&
            (event->key() == Qt::Key_A || event->key() == Qt::Key_X)) {
            int count = m_vimPending.count();
            vimAddToNumber(event->key() == Qt::Key_A ? count : -count);
            resetVimPendingState();
            return true;
        }
        return false;
    }
    if (m_cursors.size() > 1) {
        collapseToOneCursor();
    }

    if (event->key() == Qt::Key_Backspace) {
        vimExecuteMotion('h', m_vimPending.count());
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    QString text = event->text();
    /*
     * A bare modifier press is not a command. It has to be rejected on
     * the character rather than on emptiness: Shift arrives here with a
     * text of one NUL under the offscreen platform plugin, which is not
     * empty, so it used to fall through every case and hit the
     * resetVimPendingState() at the end of the dispatch — clearing a
     * count that was only half typed. `10G` went to the last line
     * instead of line ten, and every counted command needing Shift was
     * the same. See docs/adr/0082.
     */
    if (text.isEmpty() || text.at(0).isNull()) {
        return false;
    }
    QChar qc = text.at(0);

    /*
     * A remap stands in for the key before anything else looks at it,
     * so a count already typed and an operator already pending compose
     * with it exactly as they would with the key it stands for.
     *
     * Not while a pending key is awaited, though: after `f` or `r` or
     * `"` the next key is an argument, not a command, and remapping it
     * would make `f` unable to find a character you had rebound.
     */
    if (!m_vimRemapping && !m_vimPending.expectsArgument()) {
        const QString mapped = keys::vimRemap(m_config, currentModeName(), qc);
        if (!mapped.isEmpty()) {
            vimRecordKey(qc);
            m_vimRemapping = true;
            for (QChar ch : mapped) {
                QKeyEvent replay(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
                handleVimNormalOrVisualKey(&replay);
            }
            m_vimRemapping = false;
            return true;
        }
    }

    vimRecordKey(qc);

    if (vimResolvePendingKey(qc, event->key())) {
        return true;
    }
    if (vimAccumulateCount(qc)) {
        return true;
    }

    char c = qc.toLatin1(); /* '\0' for non-Latin1 — falls through to "unrecognized" below. */
    int count = m_vimPending.count();

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
