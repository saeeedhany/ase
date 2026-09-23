#include "editor_viewport.h"

#include "editor_viewport_internal.h"

#include <algorithm>
#include <cstdlib>

#include <QClipboard>
#include <QGuiApplication>
#include <QStringList>

namespace {
/* Keeps the pop-in to genuine keystrokes: a pasted block scaling in as
 * one unit looks wrong. See docs/adr/0049. */
constexpr qsizetype kTypingAnimationMaxBytes = 8;
/* "scrolloff"-style context margin kept around the cursor. */
constexpr int kVerticalScrollMargin = 3;
constexpr int kHorizontalScrollMarginChars = 4;
} // namespace

/*
 * One file's share of a multi-file edit, as a single undo group.
 *
 * Applied last-first, because every replacement shifts the offsets of
 * everything after it — and the caller has no reason to have sorted
 * them, so they are sorted here rather than trusted. Two edits on one
 * line is the case that makes this necessary and the case a producer
 * hands over most often.
 *
 * An edit naming a place the buffer does not have is skipped, not
 * fatal: a search hit can outlive the file it was found in, and half a
 * rename is worse than none of it. See docs/adr/0131.
 */
int EditorViewport::applyLineEdits(const QVector<TextEdit> &edits) {
    if (edits.isEmpty()) {
        return 0;
    }

    struct Resolved {
        size_t offset;
        int length;
        QByteArray replacement;
    };
    QVector<Resolved> resolved;
    resolved.reserve(edits.size());

    const int size = m_cache.size();
    for (const TextEdit &edit : edits) {
        if (edit.line < 1 || edit.line > m_lineStarts.size() || edit.column < 1 ||
            edit.length < 0) {
            continue;
        }
        /* offsetForLineColumn() counts from zero on both axes, and
         * clamps rather than refusing, so a 1-based value handed
         * straight to it lands one line late and silently. */
        int lineIndex = edit.line - 1;
        size_t offset = offsetForLineColumn(lineIndex, edit.column - 1);
        int lineEnd = (lineIndex + 1 < m_lineStarts.size()) ? m_lineStarts[lineIndex + 1] - 1 : size;
        /* Clamped to the line it names, not just to the buffer: a
         * length that runs off the end of the line is a stale hit, and
         * replacing across a line break is never what was meant. */
        if (static_cast<int>(offset) + edit.length > lineEnd) {
            continue;
        }
        /* The bytes have to be the ones the producer was looking at. */
        if (!edit.expected.isEmpty() &&
            m_cache.mid(static_cast<int>(offset), edit.length) != edit.expected) {
            continue;
        }
        resolved.push_back({offset, edit.length, edit.replacement});
    }
    if (resolved.isEmpty()) {
        return 0;
    }

    std::sort(resolved.begin(), resolved.end(),
              [](const Resolved &a, const Resolved &b) { return a.offset < b.offset; });

    beginUndoSession();
    beginUndoStep();
    int applied = 0;
    for (int i = resolved.size() - 1; i >= 0; --i) {
        const Resolved &edit = resolved[i];
        QByteArray removed = m_cache.mid(static_cast<int>(edit.offset), edit.length);
        if (edit.length > 0) {
            if (!ase_buffer_delete(m_buffer, edit.offset, static_cast<size_t>(edit.length))) {
                continue;
            }
            ase_undo_record_delete(m_undo, edit.offset, removed.constData(),
                                    static_cast<size_t>(removed.size()));
        }
        if (!edit.replacement.isEmpty() &&
            ase_buffer_insert(m_buffer, edit.offset, edit.replacement.constData(),
                               static_cast<size_t>(edit.replacement.size()))) {
            ase_undo_record_insert(m_undo, edit.offset, edit.replacement.constData(),
                                    static_cast<size_t>(edit.replacement.size()));
        }
        applied++;
    }
    endUndoStep();
    endUndoSession();

    collapseToOneCursor();
    refreshCache();
    m_cursors[0] = std::min(m_cursors[0], static_cast<size_t>(m_cache.size()));
    m_selectionAnchors[0] = m_cursors[0];
    ensureCursorVisible();
    update();
    return applied;
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

/* Ctrl+D. Adds a point cursor at the end of the next whole-word match.
 * Doesn't wrap. */
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

    /* A whole word, not a substring: `in` must not match inside
     * `int`. */
    auto isWholeWordAt = [&](int at) {
        if (at < 0 || at + wordLen > len || m_cache.mid(at, wordLen) != word) {
            return false;
        }
        bool before = (at == 0) || !isWordChar(m_cache[at - 1]);
        bool after = (at + wordLen == len) || !isWordChar(m_cache[at + wordLen]);
        return before && after;
    };
    /* Any caret inside the word counts: only the added ones land after it. */
    auto alreadyTaken = [&](int at) {
        for (size_t cursor : m_cursors) {
            if (cursor >= static_cast<size_t>(at) && cursor <= static_cast<size_t>(at + wordLen)) {
                return true;
            }
        }
        return false;
    };

    /* Forward from the last caret, then round to the top — see docs/adr/0138. */
    for (int pass = 0; pass < 2; ++pass) {
        const int from = (pass == 0) ? wordEnd : 0;
        const int to = (pass == 0) ? len - wordLen : wordStart - 1;
        for (int at = from; at <= to; ++at) {
            if (!isWholeWordAt(at)) {
                continue;
            }
            if (alreadyTaken(at)) {
                continue;
            }
            size_t newCursor = static_cast<size_t>(at + wordLen);
            m_cursors.push_back(newCursor);
            m_selectionAnchors.push_back(newCursor);
            normalizeCursors();
            ensureCursorVisible();
            update();
            return;
        }
    }
}

/* One cursor spanning the buffer, so it composes with everything that
 * already treats anchor != cursor as a selection. */
void EditorViewport::selectAll() {
    m_cursors = {static_cast<size_t>(m_cache.size())};
    m_selectionAnchors = {0};
    m_desiredColumns.clear();
    m_desiredColumnAt.clear();
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

/* Snapshots the range before deleting: ase_buffer_delete doesn't hand
 * back what it removed. See docs/adr/0018. */
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

/* Multiple selections join with '\n'. */
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

/* One undo group. A no-op, clipboard untouched, with no selection. */
void EditorViewport::cutSelection() {
    if (!copySelection()) {
        return;
    }
    beginUndoStep();
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        if (hasSelectionAt(i)) {
            deleteSelectionAt(i);
        }
    }
    normalizeCursors();
    endUndoStep();
    refreshCache();
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

/* The same text at every cursor, not one clipboard line per cursor. */
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

void EditorViewport::beginUndoStep() {
    if (m_undoSessionOpen) {
        return;
    }
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
}

void EditorViewport::endUndoStep() {
    if (m_undoSessionOpen) {
        return;
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
}

/* Opened where an insert begins — i/a/I/A/o/O, `c`, `R` — and closed by
 * the Escape that ends it, so the cursors recorded either side are where
 * typing started and where it stopped. */
void EditorViewport::beginUndoSession() {
    if (m_undoSessionOpen) {
        return;
    }
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_undoSessionOpen = true;
}

void EditorViewport::endUndoSession() {
    if (!m_undoSessionOpen) {
        return;
    }
    m_undoSessionOpen = false;
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
}

void EditorViewport::insertText(const QByteArray &bytes) {
    if (bytes.isEmpty()) {
        return;
    }
    /* Short single-line insertions only — an actual keystroke. */
    bool animate =
        m_animationsEnabled && bytes.size() <= kTypingAnimationMaxBytes && !bytes.contains('\n');
    beginUndoStep();
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        insertTextAt(i, bytes, animate);
    }
    normalizeCursors();
    endUndoStep();
    refreshCache();
}

/* A selection is deleted first, in the same undo group. */
void EditorViewport::insertTextAt(int i, const QByteArray &bytes, bool animate) {
    if (hasSelectionAt(i)) {
        deleteSelectionAt(i);
    }
    size_t &cursor = m_cursors[i];
    if (ase_buffer_insert(m_buffer, cursor, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, cursor, bytes.constData(), static_cast<size_t>(bytes.size()));
        if (animate) {
            /* Highest-offset-first, so no earlier iteration can have
             * written below `cursor`: this offset is stable. */
            m_typingAnimations.push_back({cursor, static_cast<size_t>(bytes.size()), 0});
        }
        cursor += static_cast<size_t>(bytes.size());
    }
    m_selectionAnchors[i] = cursor;
}

void EditorViewport::deleteBackward() {
    beginUndoStep();
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        deleteBackwardAt(i);
    }
    normalizeCursors();
    endUndoStep();
    refreshCache();
}

/* m_cache still mirrors the pre-batch buffer, but reading the
 * about-to-be-deleted bytes from it is correct: highest-offset-first
 * means nothing below this cursor has moved yet. See docs/adr/0012. */
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
    beginUndoStep();
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        deleteForwardAt(i);
    }
    normalizeCursors();
    endUndoStep();
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

/* Vim's caret sits on a character, never on the newline past the last
 * one. Only an empty line leaves it with nowhere to go. */
size_t EditorViewport::vimClampOffLineEnd(size_t offset, int line) const {
    if (!vimModeActive() || m_vimMode == VimMode::Insert) {
        return offset;
    }
    size_t lineStart = static_cast<size_t>(m_lineStarts[line]);
    if (offset <= lineStart) {
        return offset;
    }
    size_t lineEnd = (line + 1 < m_lineStarts.size())
                         ? static_cast<size_t>(m_lineStarts[line + 1] - 1)
                         : static_cast<size_t>(m_cache.size());
    if (offset < lineEnd) {
        return offset;
    }
    size_t back = offset - 1;
    while (back > lineStart && isUtf8ContinuationByte(m_cache[static_cast<int>(back)])) {
        back--;
    }
    return back;
}

void EditorViewport::ensureDesiredColumns() {
    if (m_desiredColumns.size() != m_cursors.size()) {
        /* fill(), not assign(): assign() is Qt 6.6 and the floor is 6.2. */
        m_desiredColumns.fill(-1, m_cursors.size());
        m_desiredColumnAt.fill(0, m_cursors.size());
    }
}

void EditorViewport::moveCursorVertically(int lineDelta, bool extend) {
    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorVerticallyAt(i, lineDelta, extend);
    }
    if (m_cursors.size() > 1) {
        normalizeCursors();
    }
}

void EditorViewport::moveCursorVerticallyAt(int i, int lineDelta, bool extend) {
    ensureDesiredColumns();
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = (lineDelta < 0) ? selectionMinAt(i) : selectionMaxAt(i);
        m_desiredColumns[i] = -1;
    } else {
        int line = lineForOffset(cursor);
        bool stillThere = (m_desiredColumns[i] >= 0 && m_desiredColumnAt[i] == cursor);
        int column = stillThere ? m_desiredColumns[i] : columnForOffset(cursor, line);

        int newLine = line + lineDelta;
        if (newLine >= 0 && newLine < m_lineStarts.size()) {
            cursor = vimClampOffLineEnd(offsetForLineColumn(newLine, column), newLine);
        }
        m_desiredColumns[i] = column;
        m_desiredColumnAt[i] = cursor;
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
    /* Clamped to half the viewport, so a short window degrades to less
     * context rather than oscillating. */
    int vMargin = std::min(kVerticalScrollMargin, std::max(0, (visibleLines - 1) / 2));
    if (line < m_scrollLine + vMargin) {
        m_scrollLine = line - vMargin;
    } else if (line >= m_scrollLine + visibleLines - vMargin) {
        m_scrollLine = line - visibleLines + 1 + vMargin;
    }
    m_scrollLine = std::max(0, m_scrollLine);


    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForOffset(cursor, line);
    /* xForColumn needs captures for this line, and visibleByteRange()
     * can't supply them: it answers from m_renderedScrollLine, which
     * after a jump hasn't eased here yet. Measuring outside the window
     * reads the ASE_HL_NONE fill, which lands in m_scrollX below and is
     * never recomputed. */
    ensureCaptureWindow(lineStart, lineEnd, false);
    int caretX = xForColumn(lineStart, lineEnd, col);

    int textAreaWidth = std::max(1, width() - gutterWidth());
    int hMargin = std::min(m_charWidth * kHorizontalScrollMarginChars, std::max(0, (textAreaWidth - kCaretWidth) / 2));
    if (caretX < m_scrollX + hMargin) {
        m_scrollX = caretX - hMargin;
    } else if (caretX + kCaretWidth > m_scrollX + textAreaWidth - hMargin) {
        m_scrollX = caretX + kCaretWidth - textAreaWidth + hMargin;
    }
    m_scrollX = std::max(0, m_scrollX);

    /* 1-based. Every cursor move and edit reaches here, so this is the
     * one place status needs wiring. */
    QString modeLabel;
    if (vimModeActive()) {
        switch (m_vimMode) {
        case VimMode::Normal:
            modeLabel = QStringLiteral("NORMAL");
            break;
        case VimMode::Visual:
            modeLabel = m_vimVisualLinewise ? QStringLiteral("VISUAL LINE") : QStringLiteral("VISUAL");
            break;
        case VimMode::Insert:
            modeLabel = m_vimReplacing ? QStringLiteral("REPLACE") : QStringLiteral("INSERT");
            break;
        }
    }
    emit statusChanged(line + 1, col + 1, isDirty(), modeLabel);
}

/* Applies the stack's cursor snapshot and renders instantly. */
void EditorViewport::applyUndoResult(size_t *cursors, size_t count) {
    /* The stack snapshots point offsets only, so this lands with no
     * selection. */
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

    /* Dirtiness is derived from the stack's state id, so undoing back
     * to the saved state reports clean by construction. */
    refreshCache();
    ensureCursorVisible();
    resetCaretBlink();
    /* An in-flight pop-in's range may no longer mean what it did. */
    m_typingAnimations.clear();
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
