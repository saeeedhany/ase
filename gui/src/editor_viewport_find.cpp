#include "editor_viewport.h"

#include <algorithm>

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

void EditorViewport::notifyNoMatches() {
    if (m_findNeedle.isEmpty()) {
        return;
    }
    notify(NotifyLevel::Warning, QStringLiteral("no matches for \"%1\"").arg(QString::fromUtf8(m_findNeedle)));
}

void EditorViewport::clearFindQuery() {
    m_findNeedle.clear();
    m_matches.clear();
    m_currentMatch = -1;
    update();
}

void EditorViewport::findNext() {
    if (m_matches.isEmpty()) {
        /* Reported on Enter, not while typing: incremental search passes
         * through "no matches" on the way to almost every real query, and
         * a message per keystroke would be noise. */
        notifyNoMatches();
        return;
    }
    jumpToMatch(m_currentMatch < 0 ? 0 : m_currentMatch + 1);
}

void EditorViewport::findPrevious() {
    if (m_matches.isEmpty()) {
        notifyNoMatches();
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
    refreshCache(); /* recomputes m_matches too */
    m_currentMatch = m_matches.isEmpty() ? -1 : 0;
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}
