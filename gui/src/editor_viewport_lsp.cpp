#include "editor_viewport.h"

#include "editor_viewport_internal.h"

#include "completion_popup.h"
#include "hover_panel.h"

#include <algorithm>

#include <QFileInfo>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace {
/* One piece of a hover response's `contents`: either a plain string, or
 * an object carrying a "value" string (covers both MarkupContent
 * {kind, value} and the legacy single-MarkedString {language, value}
 * shape) — see docs/adr/0030. Anything else yields an empty piece,
 * silently dropped by extractHoverText below. */
QString extractHoverPiece(const AseJsonValue *value) {
    if (value == nullptr) {
        return QString();
    }
    if (ase_json_type(value) == ASE_JSON_STRING) {
        const char *s = ase_json_get_string(value);
        return (s != nullptr) ? QString::fromUtf8(s) : QString();
    }
    if (ase_json_type(value) == ASE_JSON_OBJECT) {
        const char *v = ase_json_get_string(ase_json_object_get(value, "value"));
        return (v != nullptr) ? QString::fromUtf8(v) : QString();
    }
    return QString();
}

/* `contents` may be a single piece or (legacy MarkedString[]) an array
 * of them — joined with a blank line between, same as most editors
 * render multi-part hover. No markdown rendering (v1 simplification,
 * see docs/adr/0030) — shown as plain wrapped text either way. */
QString extractHoverText(const AseJsonValue *contents) {
    if (contents == nullptr) {
        return QString();
    }
    if (ase_json_type(contents) == ASE_JSON_ARRAY) {
        QStringList parts;
        size_t count = ase_json_array_size(contents);
        for (size_t i = 0; i < count; ++i) {
            QString piece = extractHoverPiece(ase_json_array_get(contents, i));
            if (!piece.isEmpty()) {
                parts << piece;
            }
        }
        return parts.join(QStringLiteral("\n\n"));
    }
    return extractHoverPiece(contents);
}
} // namespace

namespace {
void lspDiagnosticsTrampoline(void *user_data, const char *uri, const AseLspDiagnostic *diagnostics, size_t count) {
    static_cast<EditorViewport *>(user_data)->applyLspDiagnostics(uri, diagnostics, count);
}
void lspCompletionTrampoline(void *user_data, const AseJsonValue *result, const char *error_message) {
    static_cast<EditorViewport *>(user_data)->applyLspCompletion(result, error_message);
}
void lspHoverTrampoline(void *user_data, const AseJsonValue *result, const char *error_message) {
    static_cast<EditorViewport *>(user_data)->applyLspHover(result, error_message);
}
} // namespace

/* Gated the same way Tree-sitter syntax highlighting already is
 * (.c/.h suffix) — a language server for anything else would need a
 * per-language command mapping this v1 doesn't attempt. No default for
 * `lsp_command` (config.c) — unconfigured means no LSP for this
 * session, not a guess at which server is installed. Called from both
 * the constructor and openFile() (which stops any previous client
 * first — see its own call site) — so switching files always talks to
 * a fresh server against the right document. Editing `lsp_command`
 * itself mid-session and hot-reloading is still a documented v1 gap:
 * that only takes effect on the next file open, see docs/adr/0029. */
/* One-shot: the first time this buffer is actually shown. Keeping the
 * server alive afterwards (rather than stopping it on switch-away) is
 * deliberate — restarting clangd costs a reindex, which would make
 * switching buffers feel slow, and switching is the common action. The
 * real fix for "N visited C files means N servers" is one project-wide
 * server handling several didOpen documents; see docs/ROADMAP.md. */
void EditorViewport::onActivated() {
    setFocus();
    if (m_lspActivated) {
        return;
    }
    m_lspActivated = true;
    startLspClientIfConfigured();
}

void EditorViewport::startLspClientIfConfigured() {
    QString suffix = QFileInfo(m_filePath).suffix().toLower();
    if (suffix != QLatin1String("c") && suffix != QLatin1String("h")) {
        return;
    }
    const char *lspCommand = ase_config_get_string(m_config, "lsp_command");
    if (lspCommand == nullptr || m_filePath.isEmpty()) {
        return;
    }

    const char *argv[] = {lspCommand, nullptr};
    /* QUrl::fromLocalFile on a *relative* path (e.g. the editor was
     * launched as `ase_gui file.c` from a shell, not `ase_gui
     * /abs/path/file.c`) produces a malformed URI — real servers
     * (clangd included) reject it outright ("unresolvable URI"),
     * silently breaking every LSP feature. QFileInfo::absoluteFilePath
     * resolves against the current working directory first, matching
     * how the shell itself resolved the relative path at launch. See
     * docs/adr/0032. */
    m_lspUri = QUrl::fromLocalFile(QFileInfo(m_filePath).absoluteFilePath()).toString();
    m_lspClient = ase_lsp_client_start(argv, nullptr);
    if (m_lspClient == nullptr) {
        return;
    }

    ase_lsp_client_set_diagnostics_callback(m_lspClient, lspDiagnosticsTrampoline, this);
    ase_lsp_client_did_open(m_lspClient, m_lspUri.toUtf8().constData(), "c", m_cache.constData());
    m_lspVersion = 1;
}

void EditorViewport::pollLsp() {
    if (m_lspClient != nullptr) {
        ase_lsp_client_poll(m_lspClient);
    }
}

/* Full-document sync — see ase_lsp_client_did_change's own doc
 * comment. Called from refreshCache(), the one choke point every edit
 * already passes through, so this is the fix for "diagnostics go stale
 * after the first edit." m_cache is already a full, current mirror of
 * the buffer (ADR 0006) and, like the rest of this codebase, assumed
 * NUL-free — passing it as a C string needs no extra copy. */
void EditorViewport::sendLspDidChange() {
    if (m_lspClient == nullptr) {
        return;
    }
    m_lspVersion++;
    ase_lsp_client_did_change(m_lspClient, m_lspUri.toUtf8().constData(), m_lspVersion, m_cache.constData());
}

/* The diagnostics callback — see docs/adr/0029. `diagnostics` is
 * borrowed (valid only during this call), so every field that matters
 * is copied out, including `message` (QString, not a stored pointer).
 * A URI for a different document is ignored outright — defensive only,
 * since v1 has exactly one open document per client. */
void EditorViewport::applyLspDiagnostics(const char *uri, const AseLspDiagnostic *diagnostics, size_t count) {
    if (QString::fromUtf8(uri) != m_lspUri) {
        return;
    }
    m_diagnostics.clear();
    m_diagnostics.reserve(static_cast<int>(count));
    for (size_t i = 0; i < count; ++i) {
        GuiDiagnostic d;
        d.startLine = diagnostics[i].start.line;
        d.startChar = diagnostics[i].start.character;
        d.endLine = diagnostics[i].end.line;
        d.endChar = diagnostics[i].end.character;
        d.severity = diagnostics[i].severity;
        d.message = QString::fromUtf8(diagnostics[i].message);
        m_diagnostics.push_back(d);
    }
    update();
}

/* -------------------------------------------------------------- completion */

size_t EditorViewport::completionPrefixStart(size_t offset) const {
    size_t start = offset;
    while (start > 0 && isWordChar(m_cache[static_cast<int>(start) - 1])) {
        start--;
    }
    return start;
}

void EditorViewport::requestCompletionIfAppropriate() {
    if (m_completionPopup == nullptr) {
        return;
    }
    if (vimModeActive() && m_vimMode != VimMode::Insert) {
        /* Vim's own buffer mutations (dw, p, etc.) go through refreshCache()
         * like any other edit, which is what calls this — but a completion
         * popup responding to Normal/Visual-mode edits would be a
         * distraction, not a help. See docs/adr/0046. */
        dismissCompletion();
        return;
    }
    if (m_suppressNextCompletionTrigger) {
        m_suppressNextCompletionTrigger = false;
        dismissCompletion();
        return;
    }
    if (m_lspClient == nullptr || m_cursors.size() != 1 || hasSelectionAt(0)) {
        dismissCompletion();
        return;
    }

    size_t cursor = m_cursors[0];
    bool afterWordChar = cursor > 0 && isWordChar(m_cache[static_cast<int>(cursor) - 1]);
    /* Member-access triggers: '.' or the '>' of "->" (C has no "::").
     * Anything else is treated as "nothing worth completing here" —
     * without this gate, a request (and likely a long list of global
     * symbols) would fire after every space and newline too. */
    bool afterTrigger =
        cursor > 0 && (m_cache[static_cast<int>(cursor) - 1] == '.' ||
                       (cursor > 1 && m_cache[static_cast<int>(cursor) - 1] == '>' &&
                        m_cache[static_cast<int>(cursor) - 2] == '-'));
    if (!afterWordChar && !afterTrigger) {
        dismissCompletion();
        return;
    }

    m_completionPrefixStart = completionPrefixStart(cursor);

    int line = lineForOffset(cursor);
    AseLspPosition pos;
    pos.line = line;
    pos.character = columnForOffset(cursor, line);
    ase_lsp_client_request_completion(m_lspClient, m_lspUri.toUtf8().constData(), pos, lspCompletionTrampoline,
                                       this);
}

void EditorViewport::applyLspCompletion(const AseJsonValue *result, const char *error_message) {
    if (m_completionPopup == nullptr) {
        return;
    }
    if (error_message != nullptr || result == nullptr) {
        dismissCompletion();
        return;
    }
    applyCompletionResult(result);
}

/* `result` is either a bare CompletionItem[] or a CompletionList
 * {isIncomplete, items: [...]}  — both shapes are valid per the LSP
 * spec, servers differ on which they send. No request-sequence
 * tracking — see m_completionPrefixStart's doc comment in the header. */
void EditorViewport::applyCompletionResult(const AseJsonValue *result) {
    const AseJsonValue *itemsArray = result;
    if (ase_json_type(result) == ASE_JSON_OBJECT) {
        const AseJsonValue *items = ase_json_object_get(result, "items");
        if (items != nullptr) {
            itemsArray = items;
        }
    }
    if (ase_json_type(itemsArray) != ASE_JSON_ARRAY || m_cursors.size() != 1) {
        dismissCompletion();
        return;
    }

    QVector<CompletionPopup::Item> popupItems;
    size_t count = ase_json_array_size(itemsArray);
    for (size_t i = 0; i < count && popupItems.size() < 50; ++i) {
        const AseJsonValue *item = ase_json_array_get(itemsArray, i);
        if (item == nullptr || ase_json_type(item) != ASE_JSON_OBJECT) {
            continue;
        }
        const char *label = ase_json_get_string(ase_json_object_get(item, "label"));
        if (label == nullptr) {
            continue;
        }
        GuiCompletionItem gi;
        gi.label = QString::fromUtf8(label);
        const char *insertText = ase_json_get_string(ase_json_object_get(item, "insertText"));
        gi.insertText = (insertText != nullptr) ? QString::fromUtf8(insertText) : gi.label;
        const char *detail = ase_json_get_string(ase_json_object_get(item, "detail"));
        gi.detail = (detail != nullptr) ? QString::fromUtf8(detail) : QString();
        popupItems.push_back({gi.label, gi.insertText, gi.detail});
    }

    if (popupItems.isEmpty()) {
        dismissCompletion();
        return;
    }

    QPointF caret = caretTargetFor(m_cursors[0]);
    QPoint anchor(static_cast<int>(caret.x()), static_cast<int>(caret.y() + m_lineHeight));
    m_completionPopup->showItems(popupItems, anchor);
}

/* Replaces [m_completionPrefixStart, cursor) with the selected item's
 * insertText by giving insertText() a temporary single-cursor selection
 * over that range — it already knows how to replace an active
 * selection (docs/adr/0019), so this just reuses that path instead of
 * duplicating it. */
void EditorViewport::acceptCompletion() {
    if (m_completionPopup == nullptr || !m_completionPopup->isShowingPopup() || m_cursors.size() != 1) {
        return;
    }
    const CompletionPopup::Item *item = m_completionPopup->selectedItem();
    if (item == nullptr) {
        return;
    }

    m_selectionAnchors[0] = std::min(m_completionPrefixStart, m_cursors[0]);
    m_suppressNextCompletionTrigger = true;
    insertText(item->insertText.toUtf8());
    dismissCompletion();
    ensureCursorVisible();
    snapAnimationToTarget(); /* an edit, like typing — see docs/adr/0017 */
    update();
}

void EditorViewport::dismissCompletion() {
    if (m_completionPopup != nullptr) {
        m_completionPopup->dismiss();
    }
}

/* ------------------------------------------------------------------ hover */

void EditorViewport::scheduleHoverRequest(const QPoint &viewportPos) {
    if (m_hoverPanel == nullptr) {
        return;
    }
    size_t offset = offsetForPoint(viewportPos);

    if (m_hoverPanel->isShowingHover() && offset >= m_hoverShownRangeStart && offset < m_hoverShownRangeEnd) {
        /* Still hovering the word the open tooltip covers — no new
         * request needed, but keep it glued to the actual pointer
         * position (it eases there, doesn't jump — see
         * TrackingPopup::retarget) rather than staying pinned to
         * wherever it first appeared. */
        m_hoverPendingPos = viewportPos;
        m_hoverPanel->moveTo(viewportPos);
        return;
    }
    if (m_hoverPanel->isShowingHover()) {
        dismissHover();
    }

    m_hoverPendingPos = viewportPos;
    m_hoverPendingOffset = offset;
    m_hoverTimer->start(500); /* restarts if already running — the pause-before-request delay */
}

void EditorViewport::requestHoverNow() {
    if (m_lspClient == nullptr || m_hoverPanel == nullptr) {
        return;
    }
    int line = lineForOffset(m_hoverPendingOffset);
    AseLspPosition pos;
    pos.line = line;
    pos.character = columnForOffset(m_hoverPendingOffset, line);
    ase_lsp_client_request_hover(m_lspClient, m_lspUri.toUtf8().constData(), pos, lspHoverTrampoline, this);
}

void EditorViewport::applyLspHover(const AseJsonValue *result, const char *error_message) {
    if (m_hoverPanel == nullptr || error_message != nullptr || result == nullptr ||
        ase_json_type(result) != ASE_JSON_OBJECT) {
        /* Doesn't dismiss an already-open tooltip: with no request-
         * sequence tracking (see applyCompletionResult's doc comment),
         * an empty/failed response could belong to an earlier, already-
         * superseded request — silently contributing nothing is safer
         * than possibly clobbering a newer, good tooltip. */
        return;
    }
    applyHoverResult(result);
}

void EditorViewport::applyHoverResult(const AseJsonValue *result) {
    QString text = extractHoverText(ase_json_object_get(result, "contents"));
    if (text.isEmpty()) {
        return;
    }

    /* The response's own `range` (if present) is exactly the word/
     * token the tooltip covers — used to tell "still hovering the same
     * thing" from "moved to something else" next time the mouse moves.
     * Without one, fall back to scanning the identifier run around the
     * requested offset client-side. */
    size_t rangeStart = m_hoverPendingOffset;
    size_t rangeEnd = m_hoverPendingOffset + 1;
    const AseJsonValue *range = ase_json_object_get(result, "range");
    const AseJsonValue *start = (range != nullptr) ? ase_json_object_get(range, "start") : nullptr;
    const AseJsonValue *end = (range != nullptr) ? ase_json_object_get(range, "end") : nullptr;
    if (start != nullptr && end != nullptr) {
        int startLine = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "line"), 0));
        int startChar = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "character"), 0));
        int endLine = static_cast<int>(ase_json_get_number(ase_json_object_get(end, "line"), 0));
        int endChar = static_cast<int>(ase_json_get_number(ase_json_object_get(end, "character"), 0));
        rangeStart = offsetForLineColumn(startLine, startChar);
        rangeEnd = std::max(rangeStart + 1, offsetForLineColumn(endLine, endChar));
    } else {
        rangeStart = completionPrefixStart(m_hoverPendingOffset);
        size_t end2 = m_hoverPendingOffset;
        while (end2 < static_cast<size_t>(m_cache.size()) && isWordChar(m_cache[static_cast<int>(end2)])) {
            end2++;
        }
        rangeEnd = std::max(rangeStart + 1, end2);
    }
    m_hoverShownRangeStart = rangeStart;
    m_hoverShownRangeEnd = rangeEnd;

    m_hoverPanel->showText(text, m_hoverPendingPos);
}

void EditorViewport::dismissHover() {
    m_hoverTimer->stop();
    if (m_hoverPanel != nullptr) {
        m_hoverPanel->dismiss();
    }
}
