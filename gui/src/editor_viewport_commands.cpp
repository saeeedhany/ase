#include "editor_viewport.h"

#include "ase/recovery.h"

#include "file_browser_panel.h"
#include "output_panel.h"
#include "project_files.h"
#include "project_search.h"

namespace {
/* Separate from quick open's cap: this one bounds a search, which
 * reads every file it lists. */
constexpr int kProjectFileCap = 20000;
/* Few enough that the search stops early on a query like "e". */
constexpr int kProjectSearchHitCap = 1000;
} // namespace

#include <QRegularExpression>

#include <QDir>
#include <QFileInfo>
#include <QTimer>

/* An empty path routes to Save-As rather than silently doing nothing. */
void EditorViewport::save() {
    if (m_filePath.isEmpty()) {
        if (m_fileBrowser != nullptr) {
            m_fileBrowser->openFor(FileBrowserPanel::Mode::SaveAs);
        }
        return;
    }
    if (!ase_buffer_save_to_file(m_buffer, m_filePath.toUtf8().constData())) {
        /* A failed write was indistinguishable from a successful one. */
        notify(NotifyLevel::Error, QStringLiteral("could not write %1").arg(QFileInfo(m_filePath).fileName()));
        return;
    }
    {
        /* Every later dirty check is a comparison against this id. */
        m_savedStateId = ase_undo_state_id(m_undo);
        m_historyDiscardedWhileDirty = false;
        discardRecovery(); /* the file is the work now */
        refreshVcsMarks();  /* what changed since the commit just moved */
        ensureCursorVisible(); /* pushes the cleared dirty flag (and title) through statusChanged */
        notify(NotifyLevel::Info, QStringLiteral("saved %1").arg(QFileInfo(m_filePath).fileName()));
    }
}

/* Derived, never latched: an edit and its undo return the stack to the
 * id recorded at save. A discarded history is the one case an id
 * cannot describe, hence the flag. */
bool EditorViewport::isDirty() const {
    /* has_uncommitted as well as the id: Insert mode is one open undo
     * group, so a buffer being typed into has changed while its state id
     * has not. Without it the dirty dot stayed off mid-insert and
     * closing the buffer threw the work away without asking. */
    return m_historyDiscardedWhileDirty || ase_undo_has_uncommitted(m_undo) ||
           ase_undo_state_id(m_undo) != m_savedStateId;
}

/* Defers to save(), so dirty-clearing happens in one place. */
void EditorViewport::saveAs(const QString &path) {
    m_filePath = path;
    save();
}

namespace {

/* vim's default "magic" level against PCRE: the two disagree about which
 * of ( ) | + ? { } need a backslash, and agree about . * [ ] ^ $. So the
 * translation is mostly swapping that escaping over. Documented in
 * docs/adr/0102 — patterns are vim's dialect, not PCRE's. */
QString vimPatternToPcre(const QString &pattern) {
    static const QString swapped = QStringLiteral("()|+?{}");
    QString out;
    out.reserve(pattern.size() + 8);
    for (int i = 0; i < pattern.size(); ++i) {
        QChar c = pattern.at(i);
        if (c != QLatin1Char('\\')) {
            /* Bare, these are literal in vim and special in PCRE. */
            if (swapped.contains(c)) {
                out += QLatin1Char('\\');
            }
            out += c;
            continue;
        }
        if (i + 1 >= pattern.size()) {
            out += QStringLiteral("\\\\");
            break;
        }
        QChar next = pattern.at(++i);
        if (swapped.contains(next)) {
            out += next; /* \( is a group in vim, ( is one in PCRE */
        } else if (next == QLatin1Char('=')) {
            out += QLatin1Char('?'); /* vim's \= is "zero or one" */
        } else if (next == QLatin1Char('<') || next == QLatin1Char('>')) {
            out += QStringLiteral("\\b");
        } else {
            out += QLatin1Char('\\');
            out += next;
        }
    }
    return out;
}

/* Expanded here rather than through QString::replace, which knows
 * `\1`..`\9` but has no spelling for the whole match — and `&` is the
 * one every `:s` uses. */
QString expandReplacement(const QRegularExpressionMatch &match, const QString &spec) {
    QString out;
    out.reserve(spec.size() + match.capturedLength());
    for (int i = 0; i < spec.size(); ++i) {
        QChar c = spec.at(i);
        if (c == QLatin1Char('&')) {
            out += match.captured(0);
            continue;
        }
        if (c == QLatin1Char('\\') && i + 1 < spec.size()) {
            QChar next = spec.at(++i);
            if (next.isDigit()) {
                out += match.captured(next.digitValue());
            } else if (next == QLatin1Char('n')) {
                out += QLatin1Char('\n');
            } else if (next == QLatin1Char('t')) {
                out += QLatin1Char('\t');
            } else {
                out += next; /* \& and \\ land here, as themselves */
            }
            continue;
        }
        out += c;
    }
    return out;
}

/* One address: a number, `.`, `$`, or either of those with +/- offsets.
 * Returns false when `text` is not an address at all. */
bool parseAddress(const QString &text, int currentLine, int lastLine, int *out) {
    QString s = text.trimmed();
    if (s.isEmpty()) {
        return false;
    }
    int base = currentLine;
    int i = 0;
    if (s.at(0) == QLatin1Char('.')) {
        i = 1;
    } else if (s.at(0) == QLatin1Char('$')) {
        base = lastLine;
        i = 1;
    } else if (s.at(0).isDigit()) {
        int start = i;
        while (i < s.size() && s.at(i).isDigit()) {
            i++;
        }
        base = s.mid(start, i - start).toInt() - 1;
    }
    while (i < s.size()) {
        QChar sign = s.at(i);
        if (sign != QLatin1Char('+') && sign != QLatin1Char('-')) {
            return false;
        }
        i++;
        int start = i;
        while (i < s.size() && s.at(i).isDigit()) {
            i++;
        }
        int amount = (i > start) ? s.mid(start, i - start).toInt() : 1;
        base += (sign == QLatin1Char('+')) ? amount : -amount;
    }
    *out = base;
    return true;
}

} // namespace

/* `:[range]s/pattern/replacement/[flags]`. The floating Find/Replace is
 * untouched and remains the way to do this without vim — see
 * docs/adr/0073 for why the two surfaces exist. */
bool EditorViewport::runSubstitute(const QString &command) {
    int i = 0;
    while (i < command.size() && command.at(i) != QLatin1Char('s')) {
        i++;
    }
    if (i >= command.size()) {
        return false;
    }
    QString rangeText = command.left(i);
    QString rest = command.mid(i + 1);
    if (rest.isEmpty() || rest.at(0).isLetterOrNumber()) {
        return false; /* `set`, `sort`, ... are not this command */
    }

    int lastLine = vimLastLine();
    int currentLine = lineForOffset(m_cursors[0]);
    int firstTarget = currentLine;
    int lastTarget = currentLine;

    QString range = rangeText.trimmed();
    if (range == QLatin1String("%")) {
        firstTarget = 0;
        lastTarget = lastLine;
    } else if (!range.isEmpty()) {
        int comma = range.indexOf(QLatin1Char(','));
        if (comma < 0) {
            if (!parseAddress(range, currentLine, lastLine, &firstTarget)) {
                return false;
            }
            lastTarget = firstTarget;
        } else {
            if (!parseAddress(range.left(comma), currentLine, lastLine, &firstTarget) ||
                !parseAddress(range.mid(comma + 1), currentLine, lastLine, &lastTarget)) {
                return false;
            }
        }
    }

    QChar separator = rest.at(0);
    QStringList parts;
    QString piece;
    for (int k = 1; k < rest.size(); ++k) {
        QChar c = rest.at(k);
        if (c == QLatin1Char('\\') && k + 1 < rest.size() && rest.at(k + 1) == separator) {
            piece += separator; /* an escaped separator is data */
            k++;
            continue;
        }
        if (c == separator) {
            parts << piece;
            piece.clear();
            continue;
        }
        piece += c;
    }
    parts << piece;

    QString pattern = parts.value(0);
    QString replacement = parts.value(1);
    QString flags = parts.value(2);
    if (pattern.isEmpty()) {
        notify(NotifyLevel::Warning, QStringLiteral("no pattern"));
        return true;
    }

    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (flags.contains(QLatin1Char('i'))) {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    QRegularExpression regex(vimPatternToPcre(pattern), options);
    if (!regex.isValid()) {
        notify(NotifyLevel::Error, QStringLiteral("bad pattern: %1").arg(regex.errorString()));
        return true;
    }
    bool everyMatch = flags.contains(QLatin1Char('g'));

    firstTarget = std::clamp(firstTarget, 0, lastLine);
    lastTarget = std::clamp(lastTarget, 0, lastLine);
    if (firstTarget > lastTarget) {
        std::swap(firstTarget, lastTarget);
    }

    int changedLines = 0;
    int changedCount = 0;
    size_t landing = m_cursors[0];

    beginUndoSession();
    /* Bottom-up: replacing a line shifts every offset below it. */
    for (int line = lastTarget; line >= firstTarget; --line) {
        size_t start = static_cast<size_t>(m_lineStarts[line]);
        size_t end = vimLineEndOffset(line);
        QString text = QString::fromUtf8(m_cache.mid(static_cast<int>(start),
                                                      static_cast<int>(end - start)));
        QString updated;
        int hits = 0;
        int consumed = 0;
        QRegularExpressionMatchIterator it = regex.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            updated += text.mid(consumed, match.capturedStart() - consumed);
            updated += expandReplacement(match, replacement);
            consumed = match.capturedEnd();
            hits++;
            if (!everyMatch) {
                break;
            }
            if (match.capturedLength() == 0) {
                /* A zero-width match would otherwise sit still forever. */
                if (consumed < text.size()) {
                    updated += text.at(consumed);
                }
                consumed++;
            }
        }
        updated += text.mid(std::min(consumed, static_cast<int>(text.size())));

        if (hits == 0) {
            continue;
        }

        QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
        QByteArray inserted = updated.toUtf8();
        beginUndoStep();
        if (ase_buffer_delete(m_buffer, start, end - start)) {
            ase_undo_record_delete(m_undo, start, removed.constData(),
                                    static_cast<size_t>(removed.size()));
        }
        if (!inserted.isEmpty() &&
            ase_buffer_insert(m_buffer, start, inserted.constData(),
                               static_cast<size_t>(inserted.size()))) {
            ase_undo_record_insert(m_undo, start, inserted.constData(),
                                    static_cast<size_t>(inserted.size()));
        }
        endUndoStep();
        refreshCache();

        changedLines++;
        changedCount += hits;
        landing = start;
    }
    endUndoSession();

    if (changedCount == 0) {
        notify(NotifyLevel::Warning, QStringLiteral("pattern not found: %1").arg(pattern));
        return true;
    }

    collapseToOneCursor();
    landing = std::min(landing, static_cast<size_t>(m_cache.size()));
    m_cursors[0] = landing;
    m_selectionAnchors[0] = landing;
    vimMarkChange();
    ensureCursorVisible();
    update();
    notify(NotifyLevel::Info, QStringLiteral("%1 substitution%2 on %3 line%4")
                                   .arg(changedCount)
                                   .arg(changedCount == 1 ? QString() : QStringLiteral("s"))
                                   .arg(changedLines)
                                   .arg(changedLines == 1 ? QString() : QStringLiteral("s")));
    return true;
}

void EditorViewport::runCommand(const QString &command) {
    QString trimmed = command.trimmed();
    if (trimmed == QLatin1String("w")) {
        save();
    } else if (trimmed == QLatin1String("q")) {
        emit closeRequested(false);
    } else if (trimmed == QLatin1String("q!")) {
        emit closeRequested(true);
    } else if (trimmed == QLatin1String("compile")) {
        compile();
    } else if (trimmed == QLatin1String("output")) {
        toggleOutputPanel();
    } else if (trimmed == QLatin1String("config")) {
        openConfigFile();
    } else if (runSubstitute(trimmed)) {
        /* Reported its own outcome, including "not found". */
    } else {
        /* Not gated on vim_mode. vimGotoLine is safe either way: no
         * operator can be pending, since ex-commands bypass Vim's own
         * key dispatch. */
        bool ok = false;
        int lineNumber = trimmed.toInt(&ok);
        if (ok && lineNumber > 0) {
            recordJump();
            goToLine(lineNumber);
        } else if (!trimmed.isEmpty()) {
            if (!runPluginCommand(trimmed)) {
                notify(NotifyLevel::Warning, QStringLiteral("unknown command: %1").arg(trimmed));
            }
        }
        /* Silence made a typo look like a command that ran and did
         * nothing. */
    }
}

/* The answer to "where is the config file". It is written on first run,
 * so it is nearly always already there; re-asking covers the case where
 * it was deleted. */
void EditorViewport::openConfigFile() {
    if (m_configPath.isEmpty()) {
        notify(NotifyLevel::Error, QStringLiteral("no config path on this platform"));
        return;
    }
    QByteArray path = m_configPath.toUtf8();
    if (!ase_config_write_default_if_missing(path.constData())) {
        notify(NotifyLevel::Error, QStringLiteral("cannot create %1").arg(m_configPath));
        return;
    }
    requestOpenFile(m_configPath);
}

/* Checked last, so a plugin can't shadow a built-in.
 *
 * A plugin edits the raw AseBuffer directly — the ABI offers no way to
 * go through the undo primitives — which leaves every recorded offset
 * potentially stale, and undoing against stale offsets corrupts the
 * buffer. So the history is dropped after a successful command: losing
 * it is a visible cost, silent corruption is not. See docs/adr/0054. */
bool EditorViewport::runPluginCommand(const QString &name) {
    if (m_pluginHost == nullptr) {
        return false;
    }
    if (!ase_plugin_host_run_command(m_pluginHost, name.toUtf8().constData(), m_buffer)) {
        return false; /* no such command — the caller reports it */
    }

    /* Discarding history resets the state id to "as loaded" for a
     * buffer that isn't — the one case the id cannot express. */
    ase_undo_destroy(m_undo);
    m_undo = ase_undo_create();
    m_savedStateId = 0;
    m_historyDiscardedWhileDirty = true;

    collapseToOneCursor();
    refreshCache();
    /* The buffer may have shrunk under the cursor. */
    m_cursors[0] = std::min(m_cursors[0], static_cast<size_t>(m_cache.size()));
    m_selectionAnchors[0] = m_cursors[0];
    ensureCursorVisible();
    update();
    return true;
}

int EditorViewport::cursorColumn() const {
    if (m_cursors.isEmpty()) {
        return 1;
    }
    return columnForOffset(m_cursors[0], lineForOffset(m_cursors[0])) + 1;
}

void EditorViewport::goToLineColumn(int oneBasedLine, int oneBasedColumn) {
    collapseToOneCursor();
    size_t target = offsetForLineColumn(oneBasedLine - 1, oneBasedColumn - 1);
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    resetVimPendingState();
    ensureCursorVisible();
    resetCaretBlink();
    update();
}

void EditorViewport::goToLine(int oneBasedLine) {
    collapseToOneCursor();
    vimGotoLine(oneBasedLine - 1);
    resetVimPendingState();
    ensureCursorVisible();
    resetCaretBlink();
    update();
}

/*
 * Same file list as Ctrl+P, so a search can never hit a file quick open
 * refuses to show. Synchronous: the caps bound the worst case rather
 * than a thread doing it. See docs/adr/0066.
 */
void EditorViewport::searchProject(const QString &needle) {
    if (m_outputPanel == nullptr) {
        return;
    }
    QString trimmed = needle.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QString startDir = m_filePath.isEmpty() ? QDir::currentPath() : QFileInfo(m_filePath).absolutePath();
    QString root = project::rootFor(startDir);
    bool truncatedFileList = false;
    QStringList files = project::collect(root, kProjectFileCap, &truncatedFileList);
    project::SearchResult result = project::search(root, files, trimmed.toUtf8(), kProjectSearchHitCap);
    result.truncated = result.truncated || truncatedFileList;

    m_outputPanel->showSearchResults(root, trimmed, result);
    if (result.hits.isEmpty()) {
        /* The panel says so too, but it may be the first time it has
         * ever been shown — the message is what tells you the search
         * actually ran. */
        notify(NotifyLevel::Warning, QStringLiteral("no matches for \"%1\"").arg(trimmed));
    }
}

void EditorViewport::toggleOutputPanel() {
    if (m_outputPanel != nullptr) {
        m_outputPanel->setVisible(!m_outputPanel->isVisible());
    }
}

/* Reads build_command fresh from config on every call (not cached) so
 * an edited config.ase takes effect on the next :compile without a
 * restart, same hot-reload spirit as everything else config-driven in
 * this class. */
void EditorViewport::compile() {
    if (m_outputPanel == nullptr) {
        return;
    }
    if (m_compileProcess != nullptr) {
        m_outputPanel->appendLine(QStringLiteral("A build is already running."));
        m_outputPanel->show();
        return;
    }

    const char *buildCommand = ase_config_get_string(m_config, "build_command");
    if (buildCommand == nullptr) {
        m_outputPanel->appendLine(QStringLiteral("No build_command configured — see config.ase."));
        m_outputPanel->show();
        return;
    }
    if (m_filePath.isEmpty()) {
        m_outputPanel->appendLine(QStringLiteral("No file to compile — save it first."));
        m_outputPanel->show();
        return;
    }

    QString substituted = QString::fromUtf8(buildCommand).replace(QLatin1String("%f"), m_filePath);
    QByteArray substitutedUtf8 = substituted.toUtf8();
    QByteArray cwdUtf8 = QFileInfo(m_filePath).absolutePath().toUtf8();

    /* Run through a shell, not execvp'd directly — build_command is
     * documented (config.c's starter template) as a shell command, so
     * it can use `&&`/pipes/etc., the same way :compile's config-key
     * comment shows. */
    const char *argv[] = {"/bin/sh", "-c", substitutedUtf8.constData(), nullptr};
    m_compileProcess = ase_process_spawn(argv, cwdUtf8.constData());

    m_outputPanel->clear();
    m_outputPanel->show();
    if (m_compileProcess == nullptr) {
        m_outputPanel->appendLine(QStringLiteral("Failed to start build_command."));
        return;
    }
    m_outputPanel->appendLine(QStringLiteral("$ ") + substituted);
    m_compilePollTimer->start(100); /* same non-blocking-poll shape as ase_lsp_client_poll */
}

void EditorViewport::pollCompile() {
    if (m_compileProcess == nullptr) {
        m_compilePollTimer->stop();
        return;
    }

    char buf[4096];
    for (;;) {
        long n = ase_process_read(m_compileProcess, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        m_outputPanel->appendText(QString::fromUtf8(buf, static_cast<int>(n)));
    }

    if (ase_process_has_exited(m_compileProcess)) {
        m_outputPanel->appendLine(QStringLiteral("[exit code %1]").arg(ase_process_exit_code(m_compileProcess)));
        ase_process_destroy(m_compileProcess);
        m_compileProcess = nullptr;
        m_compilePollTimer->stop();
    }
}

/* ---- unsaved work, kept somewhere a crash cannot reach (ADR 0110) ---- */

/* Every edit restarts the timer, so a burst of typing writes one
 * snapshot at the end of it rather than one per keystroke. A buffer
 * that matches its file has nothing worth keeping. */
void EditorViewport::armRecoverySnapshot() {
    if (m_recoveryTimer == nullptr || m_recoveryDir.isEmpty() || m_filePath.isEmpty()) {
        return;
    }
    if (!isDirty()) {
        discardRecovery();
        return;
    }
    m_recoveryTimer->start(kRecoveryDelayMs);
}

void EditorViewport::writeRecoverySnapshot() {
    if (m_recoveryDir.isEmpty() || m_filePath.isEmpty() || !isDirty()) {
        return;
    }
    ase_recovery_write(m_recoveryDir.toUtf8().constData(), m_filePath.toUtf8().constData(),
                        m_cache.constData(), static_cast<size_t>(m_cache.size()));
}

bool EditorViewport::hasRecoverySnapshot() const {
    if (m_recoveryDir.isEmpty() || m_filePath.isEmpty()) {
        return false;
    }
    return ase_recovery_exists(m_recoveryDir.toUtf8().constData(), m_filePath.toUtf8().constData());
}

void EditorViewport::discardRecovery() {
    if (m_recoveryTimer != nullptr) {
        m_recoveryTimer->stop();
    }
    if (m_recoveryDir.isEmpty() || m_filePath.isEmpty()) {
        return;
    }
    ase_recovery_remove(m_recoveryDir.toUtf8().constData(), m_filePath.toUtf8().constData());
}

/* Restored as an ordinary edit, so the buffer is dirty afterwards and
 * `u` walks back to what is actually on disk. Recovering is then a
 * decision the user can reverse, not one they are stuck with. */
bool EditorViewport::restoreFromRecovery() {
    if (m_recoveryDir.isEmpty() || m_filePath.isEmpty()) {
        return false;
    }
    size_t len = 0;
    char *content = ase_recovery_read(m_recoveryDir.toUtf8().constData(),
                                       m_filePath.toUtf8().constData(), &len);
    if (content == nullptr) {
        return false;
    }

    size_t existing = ase_buffer_length(m_buffer);
    QByteArray removed = m_cache;
    beginUndoSession();
    beginUndoStep();
    if (existing > 0 && ase_buffer_delete(m_buffer, 0, existing)) {
        ase_undo_record_delete(m_undo, 0, removed.constData(), static_cast<size_t>(removed.size()));
    }
    if (len > 0 && ase_buffer_insert(m_buffer, 0, content, len)) {
        ase_undo_record_insert(m_undo, 0, content, len);
    }
    endUndoStep();
    endUndoSession();
    free(content);

    collapseToOneCursor();
    m_cursors[0] = 0;
    m_selectionAnchors[0] = 0;
    refreshCache();
    ensureCursorVisible();
    update();
    notify(NotifyLevel::Info, QStringLiteral("recovered unsaved changes"));
    return true;
}

/* Where the last session left off. Both values are clamped: the file may
 * have been edited by something else since, and a caret past the end
 * would be worse than one at the start. See docs/adr/0111. */
void EditorViewport::restorePosition(size_t cursor, int scrollLine) {
    collapseToOneCursor();
    size_t clamped = std::min(cursor, static_cast<size_t>(m_cache.size()));
    m_cursors[0] = clamped;
    m_selectionAnchors[0] = clamped;

    int lastLine = std::max(0, static_cast<int>(m_lineStarts.size()) - 1);
    m_scrollLine = std::clamp(scrollLine, 0, lastLine);
    m_renderedScrollLine = m_scrollLine;

    ensureCursorVisible();
    update();
}
