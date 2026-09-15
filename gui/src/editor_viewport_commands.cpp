#include "editor_viewport.h"

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
        ensureCursorVisible(); /* pushes the cleared dirty flag (and title) through statusChanged */
        notify(NotifyLevel::Info, QStringLiteral("saved %1").arg(QFileInfo(m_filePath).fileName()));
    }
}

/* Derived, never latched: an edit and its undo return the stack to the
 * id recorded at save. A discarded history is the one case an id
 * cannot describe, hence the flag. */
bool EditorViewport::isDirty() const {
    return m_historyDiscardedWhileDirty || ase_undo_state_id(m_undo) != m_savedStateId;
}

/* Defers to save(), so dirty-clearing happens in one place. */
void EditorViewport::saveAs(const QString &path) {
    m_filePath = path;
    save();
}

void EditorViewport::runCommand(const QString &command) {
    QString trimmed = command.trimmed();
    if (trimmed == QLatin1String("w")) {
        save();
    } else if (trimmed == QLatin1String("q")) {
        window()->close();
    } else if (trimmed == QLatin1String("compile")) {
        compile();
    } else if (trimmed == QLatin1String("output")) {
        toggleOutputPanel();
    } else if (trimmed == QLatin1String("config")) {
        openConfigFile();
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
