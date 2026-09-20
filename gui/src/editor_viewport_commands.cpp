#include "editor_viewport.h"

#include "ase/recovery.h"
#include "ase/theme.h"

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

#include <QCoreApplication>
#include <QRegularExpression>

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
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
    /* The snapshot is filed under the key, and the key is about to
     * change. Dropped before the move so the untitled one is not left
     * behind to be offered back at the next start. */
    discardRecovery();
    m_filePath = path;
    m_untitledKey.clear();
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
        /* The panel first, when there is one. `:q` means "close what I
         * am looking at", and with results on screen that is the
         * results — closing the buffer underneath them is the bigger,
         * less recoverable action. `:q!` skips this deliberately: it is
         * the escape hatch, and having to press it twice would make it
         * a worse one. See docs/adr/0118. */
        if (m_outputPanel != nullptr && m_outputPanel->isVisible()) {
            closeOutputPanel();
        } else {
            emit closeRequested(false);
        }
    } else if (trimmed == QLatin1String("q!")) {
        emit closeRequested(true);
    } else if (trimmed == QLatin1String("compile")) {
        compile();
    } else if (trimmed == QLatin1String("output")) {
        toggleOutputPanel();
    } else if (trimmed == QLatin1String("config")) {
        openConfigFile();
    } else if (trimmed == QLatin1String("theme") || trimmed.startsWith(QLatin1String("theme "))) {
        runTheme(trimmed.mid(5).trimmed());
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
            /* Every command a key can be bound to, by the same
             * resolution order, so `:editor.save` and
             * `key.ctrl+s = editor.save` mean the same thing. */
            if (!runCommandByName(trimmed)) {
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
/*
 * A plugin is handed the AseBuffer and edits it directly, so nothing it
 * does passes through the undo stack. The first version answered that
 * by throwing the history away — which meant running a formatter cost
 * you every step back to the start of the session, with no warning.
 *
 * The edit is recorded instead, as one step: the text before against
 * the text after. `u` then undoes the whole command at once, which is
 * what a single command should cost, and the rest of the history
 * survives. See docs/adr/0128.
 */
bool EditorViewport::runPluginCommand(const QString &name) {
    if (m_pluginHost == nullptr) {
        return false;
    }

    const QByteArray before = m_cache;
    if (!ase_plugin_host_run_command(m_pluginHost, name.toUtf8().constData(), m_buffer)) {
        return false; /* no such command — the caller reports it */
    }

    size_t afterLen = ase_buffer_length(m_buffer);
    QByteArray after(static_cast<int>(afterLen), Qt::Uninitialized);
    if (afterLen > 0) {
        ase_buffer_get_text(m_buffer, 0, afterLen, after.data());
    }

    if (after != before) {
        /* Put back and redone through the undo stack, so what it holds
         * describes the buffer it is attached to. */
        ase_buffer_delete(m_buffer, 0, afterLen);
        if (!before.isEmpty()) {
            ase_buffer_insert(m_buffer, 0, before.constData(), static_cast<size_t>(before.size()));
        }

        beginUndoSession();
        beginUndoStep();
        if (!before.isEmpty() && ase_buffer_delete(m_buffer, 0, static_cast<size_t>(before.size()))) {
            ase_undo_record_delete(m_undo, 0, before.constData(),
                                    static_cast<size_t>(before.size()));
        }
        if (!after.isEmpty() &&
            ase_buffer_insert(m_buffer, 0, after.constData(), static_cast<size_t>(after.size()))) {
            ase_undo_record_insert(m_undo, 0, after.constData(), static_cast<size_t>(after.size()));
        }
        endUndoStep();
        endUndoSession();
    }

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
/*
 * The same search, shown as what it would change rather than as what it
 * found. Nothing is edited here — the panel previews it and the window
 * applies it, because applying spans buffers and this class owns one.
 * See docs/adr/0131.
 */
void EditorViewport::replaceInProject(const QString &needle, const QByteArray &replacement) {
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
    /* Every occurrence, not one per line: a replace that quietly left
     * five of six on a line would be worse than one that refused. */
    project::SearchResult result =
        project::search(root, files, trimmed.toUtf8(), kProjectSearchHitCap, true);

    if (result.hits.isEmpty()) {
        notify(NotifyLevel::Warning, QStringLiteral("no matches for \"%1\"").arg(trimmed));
        return;
    }
    if (result.truncated || truncatedFileList) {
        /* Replacing a prefix of the matches while implying it was all
         * of them is the one outcome worth refusing outright. */
        notify(NotifyLevel::Error,
               QStringLiteral("too many matches for \"%1\" to replace safely — narrow it")
                   .arg(trimmed));
        return;
    }

    QVector<project::Replacement> replacements;
    replacements.reserve(result.hits.size());
    const QByteArray needleBytes = trimmed.toUtf8();
    for (const project::SearchHit &hit : result.hits) {
        /* `expected` is the needle as it is spelled *here*: the search
         * is case-insensitive, so the bytes on the line need not match
         * what was typed. Read back from the line rather than assumed. */
        QByteArray here = hit.text.toUtf8().mid(hit.textColumn - 1, needleBytes.size());
        replacements.push_back({hit, static_cast<int>(needleBytes.size()), replacement, here, true});
    }
    m_outputPanel->showReplacePreview(root, trimmed, replacement, replacements);
}

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

/*
 * Three states, one key. Escape already hands focus from the panel back
 * to the editor, and nothing handed it the other way — so once you
 * started editing, the panel was a thing you could see and not reach
 * without the mouse. See docs/adr/0118.
 */
void EditorViewport::toggleOutputPanel() {
    if (m_outputPanel == nullptr) {
        return;
    }
    if (!m_outputPanel->isVisible()) {
        m_outputPanel->show();
        m_outputPanel->focusList();
        return;
    }
    if (!m_outputPanel->hasFocusInside()) {
        /* Visible but you are typing in the buffer: go to it rather
         * than close it. Closing something you were not looking at is
         * the more annoying of the two guesses. */
        m_outputPanel->focusList();
        return;
    }
    closeOutputPanel();
}

void EditorViewport::closeOutputPanel() {
    if (m_outputPanel == nullptr || !m_outputPanel->isVisible()) {
        return;
    }
    m_outputPanel->hide();
    /* Somebody has to take the keyboard, or it goes nowhere. */
    setFocus();
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

/*
 * An unnamed buffer has no path to file a snapshot under, which is how
 * it went unsnapshotted entirely — and it is the case where unsaved
 * work is most exposed, since there is no file on disk to fall back to.
 *
 * The pid keeps two running editors apart, and the serial keeps two
 * untitled buffers in one editor apart. Neither is a path, so no real
 * file can collide with one. See docs/adr/0127.
 */
QString EditorViewport::recoveryKey() const {
    if (!m_filePath.isEmpty()) {
        return m_filePath;
    }
    if (m_untitledKey.isEmpty()) {
        static int serial = 0;
        m_untitledKey = QStringLiteral("untitled:%1:%2")
                            .arg(QCoreApplication::applicationPid())
                            .arg(++serial);
    }
    return m_untitledKey;
}

/* Every edit restarts the timer, so a burst of typing writes one
 * snapshot at the end of it rather than one per keystroke. A buffer
 * that matches its file has nothing worth keeping. */
void EditorViewport::armRecoverySnapshot() {
    if (m_recoveryTimer == nullptr || m_recoveryDir.isEmpty()) {
        return;
    }
    if (!isDirty()) {
        discardRecovery();
        return;
    }
    m_recoveryTimer->start(kRecoveryDelayMs);
}

void EditorViewport::writeRecoverySnapshot() {
    if (m_recoveryDir.isEmpty() || !isDirty()) {
        return;
    }
    ase_recovery_write(m_recoveryDir.toUtf8().constData(), recoveryKey().toUtf8().constData(),
                        m_cache.constData(), static_cast<size_t>(m_cache.size()));
}

bool EditorViewport::hasRecoverySnapshot() const {
    if (m_recoveryDir.isEmpty()) {
        return false;
    }
    return ase_recovery_exists(m_recoveryDir.toUtf8().constData(),
                                recoveryKey().toUtf8().constData());
}

void EditorViewport::discardRecovery() {
    if (m_recoveryTimer != nullptr) {
        m_recoveryTimer->stop();
    }
    if (m_recoveryDir.isEmpty()) {
        return;
    }
    ase_recovery_remove(m_recoveryDir.toUtf8().constData(), recoveryKey().toUtf8().constData());
}

/* Restored as an ordinary edit, so the buffer is dirty afterwards and
 * `u` walks back to what is actually on disk. Recovering is then a
 * decision the user can reverse, not one they are stuck with. */
bool EditorViewport::restoreFromRecovery() {
    if (m_recoveryDir.isEmpty()) {
        return false;
    }
    size_t len = 0;
    char *content = ase_recovery_read(m_recoveryDir.toUtf8().constData(),
                                       recoveryKey().toUtf8().constData(), &len);
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


/* ---- themes (ADR 0114) ---- */

/* `:theme` lists, `:theme <name>` switches for this session, and
 * `:theme save` writes the current choice into config.ase. Switching
 * does not touch the file: trying palettes should not edit something
 * the user also hand-edits, and the save is one more word. */
void EditorViewport::runTheme(const QString &argument) {
    if (argument.isEmpty()) {
        QStringList names;
        const char *active = ase_config_get_string(m_config, "theme");
        QString current = m_sessionTheme.isEmpty()
                              ? (active != nullptr ? QString::fromUtf8(active) : QString())
                              : m_sessionTheme;
        for (size_t i = 0; i < ase_theme_count(); i++) {
            QString name = QString::fromUtf8(ase_theme_at(i)->name);
            names << (name == current ? QStringLiteral("[%1]").arg(name) : name);
        }
        notify(NotifyLevel::Info, names.join(QStringLiteral("  ")));
        return;
    }

    if (argument == QLatin1String("save")) {
        QString name = m_sessionTheme;
        if (name.isEmpty()) {
            notify(NotifyLevel::Warning, QStringLiteral("no theme to save; pick one first"));
            return;
        }
        if (!writeConfigSetting(QStringLiteral("theme"), name)) {
            notify(NotifyLevel::Error, QStringLiteral("could not write %1").arg(m_configPath));
            return;
        }
        /* The file now says what the session already showed, so the
         * session override has nothing left to override. */
        m_sessionTheme.clear();
        notify(NotifyLevel::Info, QStringLiteral("saved theme = %1").arg(name));
        return;
    }

    if (ase_theme_find(argument.toUtf8().constData()) == nullptr) {
        notify(NotifyLevel::Warning, QStringLiteral("no theme called '%1'").arg(argument));
        return;
    }
    m_sessionTheme = argument;
    rebuildConfig();
    applyConfig();
    repaintForNewTheme();

    /* A colour set by hand wins over the theme's, which is the point —
     * but silently, it makes the theme look half-applied. Naming what is
     * shadowing it turns "this theme is broken" into "oh, that is my
     * line". */
    static const char *const kThemeColours[] = {
        "background",       "text",          "selection",        "find_match",
        "panel_background", "syntax_type",   "syntax_string",    "diagnostic_error",
        "diagnostic_warning"};
    QStringList shadowed;
    for (const char *colour : kThemeColours) {
        if (ase_config_is_chosen_by_hand(m_config, colour)) {
            shadowed << QString::fromLatin1(colour);
        }
    }
    if (shadowed.isEmpty()) {
        notify(NotifyLevel::Info, QStringLiteral("%1 — :theme save to keep it").arg(argument));
    } else {
        notify(NotifyLevel::Warning,
               QStringLiteral("%1 — your config still sets %2")
                   .arg(argument, shadowed.join(QStringLiteral(", "))));
    }
}

/* Rewrites one setting in config.ase, leaving every other line — and
 * every comment — exactly as it was. The file is something the user
 * hand-edits, so this replaces an existing assignment rather than
 * appending a second one that would shadow it confusingly.
 *
 * QSaveFile writes to a temporary and renames, for the same reason
 * ADR 0109 gave: a half-written config is worse than an unchanged one. */
bool EditorViewport::writeConfigSetting(const QString &key, const QString &value) {
    if (m_configPath.isEmpty()) {
        return false;
    }
    ase_config_write_default_if_missing(m_configPath.toUtf8().constData());

    QFile existing(m_configPath);
    if (!existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QStringList lines = QString::fromUtf8(existing.readAll()).split(QLatin1Char('\n'));
    existing.close();

    bool replaced = false;
    for (QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('#'))) {
            continue; /* a commented example is not the setting */
        }
        int equals = trimmed.indexOf(QLatin1Char('='));
        if (equals < 0 || trimmed.left(equals).trimmed() != key) {
            continue;
        }
        line = QStringLiteral("%1 = %2").arg(key, value);
        replaced = true;
        break;
    }
    if (!replaced) {
        if (!lines.isEmpty() && lines.last().isEmpty()) {
            lines.removeLast();
        }
        lines << QStringLiteral("%1 = %2").arg(key, value) << QString();
    }

    QSaveFile out(m_configPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    return out.commit();
}
