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
        /* What is on disk is now this exact state — remember which one
         * it was, and every later "is this dirty?" is that comparison.
         * See docs/adr/0059. */
        m_savedStateId = ase_undo_state_id(m_undo);
        m_historyDiscardedWhileDirty = false;
        ensureCursorVisible(); /* pushes the cleared dirty flag (and title) through statusChanged */
    }
}

/*
 * Derived from the buffer's actual state, never latched: an edit and its
 * undo return the stack to the id recorded at save, and the file is
 * correctly clean again. The one thing a state id cannot describe is a
 * discarded history (see runPluginCommand), which is why that flag
 * exists at all rather than being folded in.
 */
bool EditorViewport::isDirty() const {
    return m_historyDiscardedWhileDirty || ase_undo_state_id(m_undo) != m_savedStateId;
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
        } else if (!trimmed.isEmpty()) {
            runPluginCommand(trimmed);
        }
        /* Anything else recognized as neither a known word, a line
         * number, nor a registered plugin command: silent no-op — see
         * docs/adr/0025. */
    }
}

/* `:name` for any command a Lua script or native plugin registered.
 * Checked last, so a plugin can't shadow a built-in. See docs/adr/0054.
 *
 * A plugin command is handed the raw AseBuffer and edits it directly —
 * it does not go through insertText()/the undo primitives, because the
 * ABI has no way to (docs/adr/0009 registers `void(AseBuffer*, void*)`
 * and nothing more). That leaves every offset already recorded in the
 * undo stack potentially stale, and undoing against stale offsets
 * corrupts the buffer rather than merely doing the wrong thing. So the
 * undo history is dropped outright after a successful plugin command:
 * losing history is a visible, understandable cost; silent corruption
 * is not. Routing plugin edits through undo properly needs the wider
 * plugin context described in docs/EXTENSIBILITY.md, and is the main
 * reason that widening is worth doing. */
void EditorViewport::runPluginCommand(const QString &name) {
    if (m_pluginHost == nullptr) {
        return;
    }
    if (!ase_plugin_host_run_command(m_pluginHost, name.toUtf8().constData(), m_buffer)) {
        return; /* no such command — same silent no-op as any other unknown `:` word */
    }

    /* A plugin edits the buffer directly, with no undo entries for what
     * it did — so the history is thrown away rather than left pointing
     * at offsets that no longer mean anything. That resets the state id
     * to "as loaded" for a buffer that plainly isn't, and this is the
     * one case the id alone cannot express: say so explicitly. */
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
