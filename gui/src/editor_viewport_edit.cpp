#include "editor_viewport.h"

#include "editor_viewport_internal.h"

#include <algorithm>
#include <cstdlib>

#include <QClipboard>
#include <QGuiApplication>
#include <QStringList>

namespace {
/* Only insertions this short (and never crossing a line, hence the
 * '\n' check at the call site) get the typing pop-in at all: a real
 * paste or multi-line block popping in as one giant scaled unit would
 * look wrong, not good — this is only meant to fire for genuine
 * just-typed-a-character cases. See docs/adr/0049. */
constexpr qsizetype kTypingAnimationMaxBytes = 8;
/* "scrolloff"-style context margin, in lines/characters, kept visible
 * around the cursor before the view scrolls — see docs/adr/0024. */
constexpr int kVerticalScrollMargin = 3;
constexpr int kHorizontalScrollMarginChars = 4;
} // namespace

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

void EditorViewport::insertText(const QByteArray &bytes) {
    if (bytes.isEmpty()) {
        return;
    }
    /* See the kTypingAnimationMaxBytes/kTypingAnimationTicks comment —
     * restricted to short, single-line insertions, which in practice
     * means "an actual keystroke" (a plain character, or a handful of
     * bytes for a multi-byte UTF-8 one), not a paste or a completion
     * accept dumping in a whole block at once. */
    bool animate =
        m_animationsEnabled && bytes.size() <= kTypingAnimationMaxBytes && !bytes.contains('\n');
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        insertTextAt(i, bytes, animate);
    }
    normalizeCursors();
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
}

/* An active selection is replaced: delete it first (as part of the same
 * undo group), then insert at the collapse point — see docs/adr/0019. */
void EditorViewport::insertTextAt(int i, const QByteArray &bytes, bool animate) {
    if (hasSelectionAt(i)) {
        deleteSelectionAt(i);
    }
    size_t &cursor = m_cursors[i];
    if (ase_buffer_insert(m_buffer, cursor, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, cursor, bytes.constData(), static_cast<size_t>(bytes.size()));
        if (animate) {
            /* Cursors are processed highest-offset-first (the loop in
             * insertText() above), so an earlier iteration in this same
             * call can never have written before `cursor` here — this
             * offset is stable for the rest of the current call. See
             * docs/adr/0012 for the same invariant used elsewhere. */
            m_typingAnimations.push_back({cursor, static_cast<size_t>(bytes.size()), 0});
        }
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
     * docs/adr/0023. modeLabel is empty whenever Vim mode is off, so
     * the status text is byte-identical to before docs/adr/0046 for
     * anyone who hasn't opted in. */
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
            modeLabel = QStringLiteral("INSERT");
            break;
        }
    }
    emit statusChanged(line + 1, col + 1, m_dirty, modeLabel);
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
    /* Undo/redo can restore arbitrary old content — an in-flight typing
     * pop-in's byte range may no longer mean what it did when it
     * started, so drop it outright rather than let it keep animating
     * over content it was never about. */
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
