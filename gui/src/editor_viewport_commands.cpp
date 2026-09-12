#include "editor_viewport.h"

#include "file_browser_panel.h"
#include "output_panel.h"

#include <QFileInfo>
#include <QTimer>

/* Empty m_filePath (launched with no file, or a fresh openFile that
 * failed to read one) routes to the Save-As panel instead of silently
 * doing nothing — the one gap ADR 0006 flagged as deferred. */
void EditorViewport::save() {
    if (m_filePath.isEmpty()) {
        if (m_fileBrowser != nullptr) {
            m_fileBrowser->openFor(FileBrowserPanel::Mode::SaveAs);
        }
        return;
    }
    if (ase_buffer_save_to_file(m_buffer, m_filePath.toUtf8().constData())) {
        m_dirty = false;
        ensureCursorVisible(); /* pushes the cleared dirty flag (and title) through statusChanged */
    }
}

/* Destroys the current buffer/syntax/undo-history and loads `path`
 * fresh, resetting every piece of per-buffer state — cursors, scroll,
 * find query, dirty flag. Missing/unreadable files start empty with
 * `path` kept as the save target, same tolerance
 * ase_buffer_create_from_file's caller in main.cpp already had for the
 * initial launch (docs/adr/0006) — opening a not-yet-existing file by
 * name is a normal editor action, not an error. */
void EditorViewport::openFile(const QString &path) {
    /* The old client (if any) is tied to the old file's URI — stop it
     * before refreshCache() below can send it a stale-URI didChange,
     * and clear its diagnostics rather than leave them drawn against
     * the new file's unrelated content. startLspClientIfConfigured()
     * at the end starts a fresh one for the new file, same as the
     * constructor does for the initial one. */
    ase_lsp_client_stop(m_lspClient);
    m_lspClient = nullptr;
    m_diagnostics.clear();

    ase_syntax_destroy(m_syntax);
    m_syntax = nullptr;
    ase_undo_destroy(m_undo);
    ase_buffer_destroy(m_buffer);

    AseBuffer *buffer = ase_buffer_create_from_file(path.toUtf8().constData());
    if (buffer == nullptr) {
        buffer = ase_buffer_create();
    }
    m_buffer = buffer;
    m_undo = ase_undo_create();
    m_filePath = path;

    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("c") || suffix == QLatin1String("h")) {
        m_syntax = ase_syntax_create_c();
    }

    m_cursors = {0};
    m_selectionAnchors = {0};
    m_scrollLine = 0;
    m_scrollX = 0;
    m_desiredColumn = -1;
    clearFindQuery();
    m_dirty = false;

    refreshCache();
    startLspClientIfConfigured();
    ensureCursorVisible();
    resetCaretBlink();
    snapAnimationToTarget();
    update();
}

/* Sets the save target then defers to save() itself, so dirty-clearing
 * and the statusChanged emit (title included, via filePath()) happen
 * in exactly one place rather than being duplicated here. */
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
    } else {
        /* :<digits> — jump to that 1-based line. Not gated on vim_mode:
         * a generically useful command line addition, and the first one
         * to take an argument rather than match an exact string — see
         * docs/adr/0046. Reuses vimGotoLine (safe regardless of vim_mode:
         * no operator can be pending here since ex-commands don't go
         * through Vim's own key dispatch at all). */
        bool ok = false;
        int lineNumber = trimmed.toInt(&ok);
        if (ok && lineNumber > 0) {
            collapseToOneCursor();
            vimGotoLine(lineNumber - 1);
            resetVimPendingState();
            ensureCursorVisible();
            update();
        }
        /* Anything else recognized as neither a known word nor a line
         * number: silent no-op — see docs/adr/0025. */
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
