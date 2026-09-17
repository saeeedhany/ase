#include "editor_viewport.h"
#include <QHash>
#include <QFile>
#include <QDir>
#include "output_panel.h"

#include "lsp_registry.h"
#include "project_files.h"

#include "editor_viewport_internal.h"

#include "completion_popup.h"
#include "hover_panel.h"

#include <algorithm>

#include <QFileInfo>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace {
/* A plain string, or an object with a "value" — covers MarkupContent
 * and legacy MarkedString. Anything else yields an empty piece. */
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

/* One piece or an array of them, joined by a blank line. No markdown
 * rendering; plain wrapped text either way. */
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
void lspCompletionTrampoline(void *user_data, const AseJsonValue *result, const char *error_message) {
    static_cast<EditorViewport *>(user_data)->applyLspCompletion(result, error_message);
}
void lspHoverTrampoline(void *user_data, const AseJsonValue *result, const char *error_message) {
    static_cast<EditorViewport *>(user_data)->applyLspHover(result, error_message);
}
void lspReferencesTrampoline(void *user_data, const AseJsonValue *result, const char *error_message) {
    static_cast<EditorViewport *>(user_data)->applyLspReferences(result, error_message);
}
void lspDocumentSymbolsTrampoline(void *user_data, const AseJsonValue *result,
                                   const char *error_message) {
    static_cast<EditorViewport *>(user_data)->applyLspDocumentSymbols(result, error_message);
}
void lspDefinitionTrampoline(void *user_data, const AseJsonValue *result, const char *error_message) {
    static_cast<EditorViewport *>(user_data)->applyLspDefinition(result, error_message);
}
} // namespace

/* One-shot, the first time this buffer is shown. The server itself is
 * shared with every other buffer in the same language and project, so
 * this only joins one — see docs/adr/0096. */
void EditorViewport::onActivated() {
    setFocus();
    if (m_lspActivated) {
        return;
    }
    m_lspActivated = true;
    startLspClientIfConfigured();
}

/* Only on a real transition: the liveness poll runs every 750ms. */
void EditorViewport::setLspState(LspState state) {
    if (m_lspState == state) {
        return;
    }
    m_lspState = state;
    emit lspStateChanged(m_lspState, m_lspServerName);
}

/* Empty when this editor would start no server. C++ is included
 * because clangd handles both and highlighting's C-only gate is a
 * separate one (it needs a grammar per language; this does not).
 *
 * The id matters: telling a server `c` about a `.cpp` file makes the
 * resulting errors look like your code is broken. */

void EditorViewport::startLspClientIfConfigured() {
    const char *language =
        ase_config_language_for_path(m_config, m_filePath.toUtf8().constData());
    m_lspLanguageId = language != nullptr ? QString::fromUtf8(language) : QString();
    if (m_lspLanguageId.isEmpty()) {
        /* A permanently blank indicator on a .txt file is noise. */
        setLspState(LspState::NotApplicable);
        return;
    }
    /* `lang.<id>.lsp` first, so one config can name a server per
     * language; `lsp_command` stays the global fallback. */
    const char *lspCommand = ase_config_get_lang_string(m_config, language, "lsp");
    if (lspCommand == nullptr) {
        lspCommand = ase_config_get_string(m_config, "lsp_command");
    }
    if (lspCommand == nullptr || m_filePath.isEmpty() || m_lspRegistry == nullptr) {
        /* Every packaged install starts here, since lsp_command ships
         * commented out. The silence read as "LSP is broken". */
        m_lspServerName.clear();
        setLspState(LspState::Unconfigured);
        return;
    }

    /* QUrl::fromLocalFile on a relative path yields a URI clangd
     * rejects outright, silently breaking every LSP feature. */
    QString absolute = QFileInfo(m_filePath).absoluteFilePath();
    m_lspUri = QUrl::fromLocalFile(absolute).toString();

    /* Buffers in the same project share one server, so the root has to
     * be the project's, not the file's. See docs/adr/0096. */
    QString root = project::rootFor(QFileInfo(absolute).absolutePath());
    m_lspClient = m_lspRegistry->acquire(this, m_lspLanguageId,
                                          QString::fromLocal8Bit(lspCommand), root);
    if (m_lspClient == nullptr) {
        notify(NotifyLevel::Error, QStringLiteral("%1 not found — check lang.%2.lsp")
                                        .arg(m_lspServerName, m_lspLanguageId));
        return;
    }

    ase_lsp_client_did_open(m_lspClient, m_lspUri.toUtf8().constData(),
                             m_lspLanguageId.toUtf8().constData(), m_cache.constData());
    m_lspVersion = 1;
}

/* The registry owns the state; this buffer only displays it. A server
 * that fails after several buffers joined it reports to all of them. */
void EditorViewport::onLspStateChanged(LspState state, const QString &serverName) {
    m_lspServerName = serverName;
    LspState previous = m_lspState;
    setLspState(state);
    if (state == LspState::Failed && previous == LspState::Starting) {
        notify(NotifyLevel::Error, QStringLiteral("%1 not found — check lang.%2.lsp")
                                        .arg(serverName, m_lspLanguageId));
    } else if (state == LspState::Stopped) {
        notify(NotifyLevel::Error, QStringLiteral("%1 stopped — no diagnostics").arg(serverName));
    }
}

/* Lets the shared server forget this document; the server itself stays
 * up for whatever other buffers are using it. */
void EditorViewport::releaseLspClient() {
    if (m_lspRegistry == nullptr) {
        return;
    }
    if (m_lspClient != nullptr && !m_lspUri.isEmpty()) {
        ase_lsp_client_did_close(m_lspClient, m_lspUri.toUtf8().constData());
    }
    m_lspRegistry->release(this);
    m_lspClient = nullptr;
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


/*
 * Ask where the symbol under the cursor is defined.
 *
 * Unlike completion and hover, this is *asked for* rather than offered:
 * it fires on a keystroke, so every reason it can't answer is worth
 * saying out loud (docs/adr/0062's message line) instead of the
 * keystroke appearing to do nothing.
 */
void EditorViewport::goToDefinition() {
    if (m_lspClient == nullptr) {
        /* The status bar segment already says which flavour of "no
         * server" this is (docs/adr/0063); this says that *this
         * keystroke* needed one. */
        notify(NotifyLevel::Warning, QStringLiteral("no language server for this file"));
        return;
    }
    if (m_cursors.isEmpty()) {
        return;
    }

    size_t cursor = m_cursors[0];
    int line = lineForOffset(cursor);
    AseLspPosition pos;
    pos.line = line;
    pos.character = columnForOffset(cursor, line);
    ase_lsp_client_request_definition(m_lspClient, m_lspUri.toUtf8().constData(), pos,
                                       lspDefinitionTrampoline, this);
}

/*
 * textDocument/definition answers in three shapes, and a server picks
 * whichever it likes: a single Location, an array of Locations, or an
 * array of LocationLinks (which name the target range `targetRange` /
 * `targetSelectionRange` instead of `range`). clangd returns the array
 * form; handling only that would work until it doesn't.
 *
 * Multiple results are not a picker yet — the first is taken. For C,
 * "several definitions" is nearly always a declaration and its
 * definition, and the server lists the one you want first.
 */
void EditorViewport::applyLspDefinition(const AseJsonValue *result, const char *error_message) {
    if (error_message != nullptr) {
        notify(NotifyLevel::Error, QString::fromUtf8(error_message));
        return;
    }

    const AseJsonValue *location = result;
    if (result != nullptr && ase_json_type(result) == ASE_JSON_ARRAY) {
        if (ase_json_array_size(result) == 0) {
            location = nullptr;
        } else {
            location = ase_json_array_get(result, 0);
        }
    }
    if (location == nullptr || ase_json_type(location) != ASE_JSON_OBJECT) {
        notify(NotifyLevel::Warning, QStringLiteral("no definition found"));
        return;
    }

    /* Location vs LocationLink. */
    const char *uri = ase_json_get_string(ase_json_object_get(location, "uri"));
    const AseJsonValue *range = ase_json_object_get(location, "range");
    if (uri == nullptr) {
        uri = ase_json_get_string(ase_json_object_get(location, "targetUri"));
        range = ase_json_object_get(location, "targetSelectionRange");
        if (range == nullptr) {
            range = ase_json_object_get(location, "targetRange");
        }
    }
    const AseJsonValue *start = (range != nullptr) ? ase_json_object_get(range, "start") : nullptr;
    if (uri == nullptr || start == nullptr) {
        notify(NotifyLevel::Warning, QStringLiteral("no definition found"));
        return;
    }

    int targetLine = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "line"), 0)) + 1;
    QString path = QUrl(QString::fromUtf8(uri)).toLocalFile();
    if (path.isEmpty()) {
        /* A non-file URI — a built-in, or something inside an archive.
         * Nothing to open, and silence would look like a broken key. */
        notify(NotifyLevel::Warning, QStringLiteral("definition is not in a file"));
        return;
    }

    /* Recorded here, not when the request was sent: a lookup that found
     * nothing must not leave a phantom entry in the history. */
    recordJump();
    if (QFileInfo(path) == QFileInfo(m_filePath)) {
        /* Already here: jump without asking the window to "open" a file
         * that is on screen, which would otherwise just re-activate this
         * buffer and lose the point of the animation. */
        goToLine(targetLine);
        return;
    }
    emit fileOpenAtLineRequested(path, targetLine);
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

/* ---- find references and document symbols (ADR 0116) ---- */

namespace {

/* A Location or a LocationLink: the same pair of shapes
 * applyLspDefinition has to cope with, since a server picks either. */
bool locationFrom(const AseJsonValue *entry, QString *path, int *line, int *column) {
    if (entry == nullptr || ase_json_type(entry) != ASE_JSON_OBJECT) {
        return false;
    }
    const char *uri = ase_json_get_string(ase_json_object_get(entry, "uri"));
    const AseJsonValue *range = ase_json_object_get(entry, "range");
    if (uri == nullptr) {
        uri = ase_json_get_string(ase_json_object_get(entry, "targetUri"));
        range = ase_json_object_get(entry, "targetSelectionRange");
        if (range == nullptr) {
            range = ase_json_object_get(entry, "targetRange");
        }
    }
    const AseJsonValue *start = (range != nullptr) ? ase_json_object_get(range, "start") : nullptr;
    if (uri == nullptr || start == nullptr) {
        return false;
    }
    QString local = QUrl(QString::fromUtf8(uri)).toLocalFile();
    if (local.isEmpty()) {
        return false; /* a built-in, or something inside an archive */
    }
    *path = local;
    *line = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "line"), 0)) + 1;
    /* LSP counts characters in UTF-16 code units and this treats them as
     * bytes. Identical for ASCII, which identifiers overwhelmingly are;
     * on a line with wider characters before the symbol the caret lands
     * a little early, which is recoverable in a way a wrong line is
     * not. */
    *column = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "character"), 0)) + 1;
    return true;
}

/* The identifier run around `offset`, for naming what was asked about.
 * Only for the summary line — the server is the one that decides what
 * the symbol actually is. */
QString identifierAt(const QByteArray &cache, size_t offset) {
    int len = cache.size();
    int at = std::min(static_cast<int>(offset), std::max(0, len - 1));
    if (len == 0 || !isWordChar(cache[at])) {
        return QStringLiteral("this");
    }
    int start = at;
    while (start > 0 && isWordChar(cache[start - 1])) {
        start--;
    }
    int end = at;
    while (end + 1 < len && isWordChar(cache[end + 1])) {
        end++;
    }
    return QString::fromUtf8(cache.mid(start, end - start + 1));
}

/* The referenced lines themselves, so the list reads as code rather
 * than as coordinates. Grouped by file so each is read once however
 * many hits it holds. */
void fillLineText(QVector<project::SearchHit> &hits) {
    QHash<QString, QVector<int>> wanted;
    for (int i = 0; i < hits.size(); ++i) {
        wanted[hits[i].path].push_back(i);
    }
    for (auto it = wanted.constBegin(); it != wanted.constEnd(); ++it) {
        QFile file(it.key());
        if (!file.open(QIODevice::ReadOnly)) {
            continue; /* deleted since the server indexed it */
        }
        const QList<QByteArray> lines = file.readAll().split('\n');
        for (int index : it.value()) {
            int line = hits[index].line;
            if (line >= 1 && line <= lines.size()) {
                hits[index].text = QString::fromUtf8(lines.at(line - 1)).trimmed();
            }
        }
    }
}

} // namespace

void EditorViewport::findReferences() {
    if (m_lspClient == nullptr) {
        notify(NotifyLevel::Warning, QStringLiteral("no language server for this file"));
        return;
    }
    if (m_cursors.isEmpty()) {
        return;
    }
    /* Kept for the summary line: by the time the answer arrives the
     * caret may have moved, and "12 references to x" would then name
     * the wrong symbol. */
    m_lspReferenceSymbol = identifierAt(m_cache, m_cursors[0]);

    size_t cursor = m_cursors[0];
    int line = lineForOffset(cursor);
    AseLspPosition pos;
    pos.line = line;
    pos.character = columnForOffset(cursor, line);
    ase_lsp_client_request_references(m_lspClient, m_lspUri.toUtf8().constData(), pos, true,
                                       lspReferencesTrampoline, this);
}

void EditorViewport::applyLspReferences(const AseJsonValue *result, const char *error_message) {
    if (error_message != nullptr) {
        notify(NotifyLevel::Error, QString::fromUtf8(error_message));
        return;
    }
    if (result == nullptr || ase_json_type(result) != ASE_JSON_ARRAY ||
        ase_json_array_size(result) == 0) {
        notify(NotifyLevel::Warning, QStringLiteral("no references found"));
        return;
    }

    QString root = project::rootFor(m_filePath.isEmpty() ? QDir::currentPath()
                                                          : QFileInfo(m_filePath).absolutePath());
    QDir rootDir(root);
    QVector<project::SearchHit> hits;
    for (size_t i = 0; i < ase_json_array_size(result); i++) {
        project::SearchHit hit;
        QString absolute;
        if (!locationFrom(ase_json_array_get(result, i), &absolute, &hit.line, &hit.column)) {
            continue;
        }
        /* Relative to the project root, like every other row in this
         * panel; an absolute path would push the line text off screen. */
        hit.path = rootDir.relativeFilePath(absolute);
        hits.push_back(hit);
    }
    if (hits.isEmpty()) {
        notify(NotifyLevel::Warning, QStringLiteral("no references found"));
        return;
    }
    fillLineText(hits);

    if (m_outputPanel != nullptr) {
        m_outputPanel->showLocations(
            root,
            QStringLiteral("%1 %2 to \"%3\"")
                .arg(hits.size())
                .arg(hits.size() == 1 ? QStringLiteral("reference") : QStringLiteral("references"))
                .arg(m_lspReferenceSymbol),
            hits);
    }
}

/*
 * textDocument/documentSymbol answers in two shapes and the server
 * picks: a flat SymbolInformation array, where each entry carries a
 * `location`, or a nested DocumentSymbol tree, where each carries a
 * `range` and may carry `children`. clangd returns the nested one.
 * Reading only that would work until a server that doesn't is used.
 */
namespace {

/* LSP SymbolKind, for the few worth distinguishing at a glance. The
 * rest are unlabelled rather than guessed at. */
const char *symbolKindName(int kind) {
    switch (kind) {
    case 5: return "class";
    case 6: return "method";
    case 8: return "field";
    case 9: return "ctor";
    case 10: return "enum";
    case 11: return "interface";
    case 12: return "fn";
    case 13: return "var";
    case 14: return "const";
    case 22: return "enum";
    case 23: return "struct";
    case 26: return "type";
    default: return nullptr;
    }
}

void collectSymbols(const AseJsonValue *array, int depth, const QString &path,
                    QVector<project::SearchHit> &out) {
    if (array == nullptr || ase_json_type(array) != ASE_JSON_ARRAY) {
        return;
    }
    for (size_t i = 0; i < ase_json_array_size(array); i++) {
        const AseJsonValue *entry = ase_json_array_get(array, i);
        if (entry == nullptr || ase_json_type(entry) != ASE_JSON_OBJECT) {
            continue;
        }
        const char *name = ase_json_get_string(ase_json_object_get(entry, "name"));
        if (name == nullptr) {
            continue;
        }

        /* DocumentSymbol keeps its range at the top level;
         * SymbolInformation buries it under `location`. */
        const AseJsonValue *range = ase_json_object_get(entry, "selectionRange");
        if (range == nullptr) {
            range = ase_json_object_get(entry, "range");
        }
        if (range == nullptr) {
            const AseJsonValue *location = ase_json_object_get(entry, "location");
            if (location != nullptr) {
                range = ase_json_object_get(location, "range");
            }
        }
        const AseJsonValue *start = (range != nullptr) ? ase_json_object_get(range, "start") : nullptr;
        if (start == nullptr) {
            continue;
        }

        project::SearchHit hit;
        hit.path = path;
        hit.line = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "line"), 0)) + 1;
        hit.column =
            static_cast<int>(ase_json_get_number(ase_json_object_get(start, "character"), 0)) + 1;

        /* Indented by nesting, so a method reads as belonging to its
         * class rather than as another top-level name. */
        const char *kind = symbolKindName(
            static_cast<int>(ase_json_get_number(ase_json_object_get(entry, "kind"), 0)));
        hit.text = QString(depth * 2, QLatin1Char(' ')) +
                   (kind != nullptr ? QStringLiteral("%1 %2").arg(QLatin1String(kind),
                                                                   QString::fromUtf8(name))
                                    : QString::fromUtf8(name));
        out.push_back(hit);

        collectSymbols(ase_json_object_get(entry, "children"), depth + 1, path, out);
    }
}

} // namespace

void EditorViewport::showDocumentSymbols() {
    if (m_lspClient == nullptr) {
        notify(NotifyLevel::Warning, QStringLiteral("no language server for this file"));
        return;
    }
    ase_lsp_client_request_document_symbols(m_lspClient, m_lspUri.toUtf8().constData(),
                                             lspDocumentSymbolsTrampoline, this);
}

void EditorViewport::applyLspDocumentSymbols(const AseJsonValue *result,
                                              const char *error_message) {
    if (error_message != nullptr) {
        notify(NotifyLevel::Error, QString::fromUtf8(error_message));
        return;
    }
    QString root = project::rootFor(m_filePath.isEmpty() ? QDir::currentPath()
                                                          : QFileInfo(m_filePath).absolutePath());
    QVector<project::SearchHit> hits;
    collectSymbols(result, 0, QDir(root).relativeFilePath(m_filePath), hits);

    if (hits.isEmpty()) {
        notify(NotifyLevel::Warning, QStringLiteral("no symbols in this file"));
        return;
    }
    if (m_outputPanel != nullptr) {
        m_outputPanel->showLocations(root,
                                      QStringLiteral("%1 %2 in %3")
                                          .arg(hits.size())
                                          .arg(hits.size() == 1 ? QStringLiteral("symbol")
                                                                : QStringLiteral("symbols"))
                                          .arg(QFileInfo(m_filePath).fileName()),
                                      hits);
    }
}
