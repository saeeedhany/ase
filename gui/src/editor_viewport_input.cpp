#include "editor_viewport.h"
#include "output_panel.h"
#include "command_registry.h"
#include "keybindings.h"

#include "about_panel.h"
#include "command_line.h"
#include "completion_popup.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "list_navigation.h"
#include "help_panel.h"

#include <algorithm>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

/* Enter/Tab accept, Escape dismisses, Ctrl+J/K and the arrows move;
 * anything else falls through and retriggers a fresh request. */
bool EditorViewport::handleCompletionPopupKey(QKeyEvent *event) {
    if (m_completionPopup == nullptr || !m_completionPopup->isShowingPopup()) {
        return false;
    }
    /* Before the switch: a modifier combination, not a bare key. */
    if (int delta = listnav::delta(event); delta != 0) {
        m_completionPopup->moveSelection(delta);
        return true;
    }
    switch (event->key()) {
    case Qt::Key_Escape:
        dismissCompletion();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Tab:
        acceptCompletion();
        return true;
    default:
        break;
    }
    return false;
}

/*
 * Every chord the editor answers to is a name in the registry and a row
 * in keys::defaults(). What used to be two chains of `if (key == ...)`
 * is now one lookup, which is what lets a user rebind any of it and a
 * plugin be bound to a key without new API. See docs/adr/0113.
 */
void EditorViewport::registerCommands(CommandRegistry *registry) {
    m_commands = registry;
    if (registry == nullptr) {
        return;
    }
    /*
     * Into this viewport's own table, not the shared one.
     *
     * The shared registry holds what the window owns — the buffer list,
     * the jumplist, the panel's size. Everything below acts on *a*
     * buffer, and a lambda capturing `this` is only right for the
     * buffer it was made from.
     *
     * The first version put these in the shared table and skipped names
     * already there, so the first buffer opened owned every editor
     * command for the rest of the session: with two files open, Ctrl+S
     * saved the first one whatever was on screen, and reported nothing
     * because that buffer was clean. See docs/adr/0119.
     */
    auto add = [this](const char *name, const char *description, std::function<void()> run) {
        m_ownCommands.add(QString::fromLatin1(name), QString::fromLatin1(description),
                           std::move(run));
    };

    add("editor.save", "Save the file", [this]() { save(); });
    add("editor.save-as", "Save under a different name", [this]() {
        if (m_fileBrowser != nullptr) {
            m_fileBrowser->openFor(FileBrowserPanel::Mode::SaveAs);
        }
    });
    add("editor.open", "Open a file", [this]() {
        if (m_fileBrowser != nullptr) {
            m_fileBrowser->openFor(FileBrowserPanel::Mode::Open);
        }
    });
    add("editor.open-in-project", "Open any file in the project by name", [this]() {
        if (m_fileBrowser != nullptr) {
            m_fileBrowser->openFor(FileBrowserPanel::Mode::QuickOpen);
        }
    });
    add("editor.quit", "Quit", [this]() { window()->close(); });

    add("editor.find", "Find in this buffer", [this]() {
        if (m_findBar != nullptr) {
            m_findBar->openFor(FindBar::Mode::Find);
        }
    });
    add("editor.find-in-project", "Find across the project", [this]() {
        if (m_findBar != nullptr) {
            m_findBar->openFor(FindBar::Mode::Project);
        }
    });
    add("editor.rename-symbol", "Rename the symbol everywhere", [this]() {
        if (m_findBar != nullptr) {
            m_findBar->openFor(FindBar::Mode::Rename);
        }
    });
    add("editor.replace-in-project", "Replace across the project", [this]() {
        if (m_findBar != nullptr) {
            m_findBar->openFor(FindBar::Mode::ProjectReplace);
        }
    });
    add("editor.replace", "Find and replace", [this]() {
        if (m_findBar != nullptr) {
            m_findBar->openFor(FindBar::Mode::Replace);
        }
    });

    add("editor.help", "Show every keyboard shortcut", [this]() {
        if (m_helpPanel != nullptr) {
            m_helpPanel->openHelp();
        }
    });
    add("editor.about", "About this editor", [this]() {
        if (m_aboutPanel == nullptr) {
            m_aboutPanel = new AboutPanel(this);
        }
        m_aboutPanel->openAbout();
    });
    add("editor.output-panel", "Toggle the build output", [this]() { toggleOutputPanel(); });

    add("editor.copy", "Copy", [this]() { copySelection(); });
    add("editor.cut", "Cut", [this]() { cutSelection(); });
    add("editor.paste", "Paste the system clipboard", [this]() { pasteClipboard(); });
    add("editor.select-all", "Select the whole buffer", [this]() { selectAll(); });
    add("editor.undo", "Undo", [this]() { undo(); });
    add("editor.redo", "Redo", [this]() { redo(); });
    add("editor.cursor.add-next-occurrence", "Add a cursor at the next match",
        [this]() { addCursorAtNextOccurrence(); });

    add("editor.go-to-definition", "Go to definition", [this]() { goToDefinition(); });
    add("editor.find-references", "Every use of the symbol under the cursor",
        [this]() { findReferences(); });
    add("editor.document-symbols", "Outline of this file",
        [this]() { showDocumentSymbols(); });

    add("editor.font.larger", "Larger text", [this]() { adjustFontSize(1); });
    add("editor.font.smaller", "Smaller text", [this]() { adjustFontSize(-1); });
    add("editor.font.reset", "Text back to the configured size", [this]() { resetFontSize(); });

    add("editor.compile", "Run the build command", [this]() { compile(); });

    add("vim.half-page-down", "Half a screen down", [this]() { vimHalfPageMotion(1); });
    add("vim.half-page-up", "Half a screen up", [this]() { vimHalfPageMotion(-1); });
    add("vim.number.increment", "Add one to the next number",
        [this]() { vimAddToNumber(std::max(1, m_vimPending.count())); });
    add("vim.number.decrement", "Subtract one from the next number",
        [this]() { vimAddToNumber(-std::max(1, m_vimPending.count())); });

    /* Now that every name exists, the config can be checked against
     * them. */
    reportKeybindingProblems();
}

/* Empty when Vim mode is off, so a Vim-only binding simply does not
 * match and a non-Vim user never sees it. */
bool EditorViewport::runNamedCommand(const QString &name) {
    return m_ownCommands.run(name);
}

/*
 * One resolution order, for every way of naming a command: this
 * buffer's own, then the window's, then whatever a plugin registered.
 *
 * Shared by key bindings and by the `:` line, because they had drifted.
 * A binding could name `editor.save` and a key would run it, while
 * typing `:editor.save` reported an unknown command — the same name
 * meaning two different things depending on how you said it. See
 * docs/adr/0128.
 *
 * A name in two places would mean the viewport shadowing the window,
 * which no built-in does: they are `editor.*` and `buffer.*`/`pane.*`.
 */
bool EditorViewport::runCommandByName(const QString &name) {
    if (m_ownCommands.run(name)) {
        return true;
    }
    if (m_commands != nullptr && m_commands->run(name)) {
        return true;
    }
    return runPluginCommand(name);
}

QString EditorViewport::currentModeName() const {
    if (!vimModeActive()) {
        return QString();
    }
    switch (m_vimMode) {
    case VimMode::Normal:
        return QStringLiteral("normal");
    case VimMode::Insert:
        return QStringLiteral("insert");
    case VimMode::Visual:
        return QStringLiteral("visual");
    }
    return QString();
}

bool EditorViewport::handleBoundChord(QKeyEvent *event) {
    if (m_commands == nullptr) {
        return false;
    }
    /* Only chords a modifier or a function key names. Without this a
     * user could bind `a` and make the editor untypeable, and every
     * printable keystroke would go through a hash lookup on its way to
     * the buffer. Shift alone does not count: Shift+A is typing. */
    bool chorded = (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0;
    bool functionKey = event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F12;
    if (!chorded && !functionKey) {
        return false;
    }
    QString chord = keys::chordFor(event);
    if (chord.isEmpty()) {
        return false;
    }
    QString command = keys::commandFor(m_config, chord, currentModeName());
    if (command.isEmpty()) {
        return false;
    }
    /* `none` is how a user switches a default off. It is handled, not
     * unbound: falling through would hand the chord to whatever comes
     * next, which is not what "off" means. */
    if (command == QLatin1String("none")) {
        return true;
    }
    /* Plugins register with the host rather than the registry, and
     * runCommandByName() reaches both. That is what makes
     * `key.f5 = myplugin.reformat` work with no new API — the claim
     * ADR 0113 makes. */
    if (runCommandByName(command)) {
        return true;
    }
    /* A binding naming nothing at all. Silence would be
     * indistinguishable from a dead key. */
    notify(NotifyLevel::Warning, QStringLiteral("no command called '%1'").arg(command));
    return true;
}

void EditorViewport::keyPressEvent(QKeyEvent *event) {
    /* Belt-and-braces: a modal panel normally holds focus anyway, but
     * no focus-routing edge case may edit the document beneath. */
    if (isModalPanelOpen()) {
        QWidget::keyPressEvent(event);
        return;
    }

    /* Before anything interprets it: a macro should replay the keys the
     * user pressed, not the actions they turned into. */
    vimRecordMacroKey(event);

    resetCaretBlink();
    dismissHover();
    if (handleCompletionPopupKey(event)) {
        return;
    }

    bool extend = event->modifiers() & Qt::ShiftModifier;

    if (event->key() == Qt::Key_Up) {
        moveCursorVertically(-1, extend);
        ensureCursorVisible();
        update();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        moveCursorVertically(1, extend);
        ensureCursorVisible();
        update();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (vimModeActive() && m_vimMode == VimMode::Insert) {
            /* The count's extra passes run from where typing stopped, so
             * they have to happen before the cursor steps back — doing it
             * after made `3Rab` start a character early and produce
             * "aababfgh" instead of "abababgh". */
            if (m_vimReplacing) {
                vimLeaveReplaceMode();
            }
            /* Steps back a column, as vim does, unless already at 0. */
            if (m_cursors[0] > static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])) {
                moveCursorLeftAt(0, false);
            }
            m_vimMode = VimMode::Normal;
            vimEndInsertCapture();
            endUndoSession();
            resetVimPendingState();
        } else if (vimModeActive()) {
            /* Drop any pending operator/count and selection. */
            resetVimPendingState();
            collapseToOneCursor();
            m_vimMode = VimMode::Normal;
            m_vimVisualLinewise = false;
        } else {
            collapseToOneCursor();
        }
        /* Vim's :nohlsearch, on the key people actually press for it.
         * The needle survives, so `n` still works afterwards. */
        clearFindHighlights();
        ensureCursorVisible();
        update();
        return;
    }
    /* F1 and F12 were hardcoded here, above the binding dispatch, so
     * they never reached the table that claims to own every chord —
     * rebinding either did nothing, and Shift+F12 arrived as F12. They
     * are rows in keys::defaults() like everything else now. */
    m_desiredColumn = -1;

    if (vimModeActive() && m_vimMode != VimMode::Insert) {
        if (handleVimNormalOrVisualKey(event)) {
            return;
        }
        /* Falls through only for keys Vim doesn't claim. */
    }

    /* Before the switch below, which claims F1, F12, Escape and the
     * arrows by key code. A bound chord is a command whatever key it
     * names, and the gate inside keeps this off the typing path. */
    if (handleBoundChord(event)) {
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left:
        moveCursorLeft(extend);
        break;
    case Qt::Key_Right:
        moveCursorRight(extend);
        break;
    case Qt::Key_Home:
        moveCursorHome(extend);
        break;
    case Qt::Key_End:
        moveCursorEnd(extend);
        break;
    case Qt::Key_Backspace:
        if (m_vimReplacing && vimReplaceBackspace()) {
            if (m_dotCapturingInsert && !m_dotInsertBuf.isEmpty()) {
                int i = m_dotInsertBuf.size() - 1;
                while (i > 0 && (static_cast<unsigned char>(m_dotInsertBuf.at(i)) & 0xC0) == 0x80) {
                    i--;
                }
                m_dotInsertBuf.truncate(i);
            }
            break;
        }
        /* The capture holds what the session actually put in the buffer,
         * so a correction has to shorten it rather than be recorded as a
         * keystroke — otherwise typing "helo", Backspace, "lo" repeats
         * as "helolo". Steps back over a whole codepoint. */
        if (m_dotCapturingInsert && !m_dotInsertBuf.isEmpty()) {
            int i = m_dotInsertBuf.size() - 1;
            while (i > 0 && (static_cast<unsigned char>(m_dotInsertBuf.at(i)) & 0xC0) == 0x80) {
                i--;
            }
            m_dotInsertBuf.truncate(i);
        }
        deleteBackward();
        break;
    case Qt::Key_Delete:
        /* Vim gives Delete no Normal-mode meaning either. */
        if (vimModeActive() && m_vimMode != VimMode::Insert) {
            break;
        }
        deleteForward();
        break;
    case Qt::Key_Tab:
        /* Four spaces, not a literal tab: the render path measures
         * each run with horizontalAdvance and no tab-stop expansion, so
         * a raw '\t' would measure ~0 wide and show as no indent at
         * all. See docs/adr/0031. */
        if (vimModeActive() && m_vimMode != VimMode::Insert) {
            break;
        }
        if (m_dotCapturingInsert) {
            m_dotInsertBuf.append("    ");
        }
        if (m_vimReplacing) {
            vimReplaceTyped(QByteArrayLiteral("    "));
        } else {
            insertText(QByteArrayLiteral("    "));
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        /* Vim gives Enter no Normal-mode meaning either. */
        if (vimModeActive() && m_vimMode != VimMode::Insert) {
            break;
        }
        if (m_dotCapturingInsert) {
            m_dotInsertBuf.append('\n');
        }
        if (m_vimReplacing) {
            vimReplaceTyped(QByteArrayLiteral("\n"));
        } else {
            insertText(QByteArrayLiteral("\n"));
        }
        break;
    default:
        {
            const QString text = event->text();
            if (text.isEmpty() || !text.at(0).isPrint()) {
                QWidget::keyPressEvent(event);
                return;
            }
            /* Defense-in-depth, not the primary enforcement point: the
             * gate above (m_desiredColumn = -1's neighbor) already
             * claims every printable key while Vim mode is on and not
             * in Insert, so this should be unreachable in that state —
             * see docs/adr/0046. */
            if (vimModeActive() && m_vimMode != VimMode::Insert) {
                return;
            }
            if (m_dotCapturingInsert) {
                m_dotInsertBuf.append(text.toUtf8());
            }
            if (m_vimReplacing) {
                vimReplaceTyped(text.toUtf8());
            } else {
                insertText(text.toUtf8());
            }
        }
    }

    ensureCursorVisible();
    update();
}

void EditorViewport::wheelEvent(QWheelEvent *event) {
    /* See docs/adr/0044 — a modal panel doesn't cover the whole
     * viewport, so without this the document visible around/behind it
     * could still be scrolled while it's open. */
    if (isModalPanelOpen()) {
        QWidget::wheelEvent(event);
        return;
    }

    /* Scrolling moves the caret's on-screen position without moving the
     * cursor itself — both popups anchor to a screen point computed at
     * request time, so neither would track the new scroll offset. See
     * docs/adr/0030. */
    dismissCompletion();
    dismissHover();
    int lines = event->angleDelta().y() / 40;
    int maxScroll = std::max(0, static_cast<int>(m_lineStarts.size()) - 1);
    m_scrollLine = std::clamp(m_scrollLine - lines, 0, maxScroll);
    update();
}

void EditorViewport::mousePressEvent(QMouseEvent *event) {
    /* Blocks the click outright rather than also treating it as
     * "clicked away, so close the panel" — a non-interactive widget
     * inside an open panel (a QLabel, empty layout space) ignores a
     * press and Qt bubbles it up to here exactly the same way a
     * genuine outside click would arrive, so the two aren't reliably
     * distinguishable at this level; a real double-click inside a
     * panel bubbles the same way too. Rather than chase every such
     * case, closing is Escape-only (each panel's own eventFilter
     * handles that directly) — a click while a panel is open just does
     * nothing. See docs/adr/0044. */
    if (isModalPanelOpen()) {
        return;
    }

    dismissCompletion();
    dismissHover();

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    size_t offset = offsetForPoint(event->position().toPoint());

    if (event->modifiers() & Qt::ShiftModifier) {
        /* Extends the primary (last) cursor's selection to the click
         * point instead of clearing — its anchor is left untouched, so a
         * fresh Shift+click after a plain click anchors at the old
         * cursor position. Multi-cursor + Shift+click isn't a scoped
         * combination (drag-selection is single-cursor-only too, see
         * mouseMoveEvent) — only the last cursor is affected. */
        if (!m_cursors.isEmpty()) {
            m_cursors.last() = offset;
        }
    } else if (event->modifiers() & Qt::AltModifier) {
        m_cursors.push_back(offset);
        m_selectionAnchors.push_back(offset);
        normalizeCursors();
    } else {
        m_cursors.clear();
        m_selectionAnchors.clear();
        m_cursors.push_back(offset);
        m_selectionAnchors.push_back(offset);
    }

    m_desiredColumn = -1;
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

/* While the left button is held, Qt keeps delivering move events to this
 * widget (implicit press-grab) regardless of setMouseTracking — no extra
 * grab needed. Only extends the single-cursor case: a plain press already
 * collapsed to one cursor, so a drag starting from a multi-cursor state
 * can't happen. See docs/adr/0019. */
void EditorViewport::mouseMoveEvent(QMouseEvent *event) {
    /* See docs/adr/0044 — skips both the passive-hover-popup branch and
     * drag-select below while a modal panel is open, same reasoning as
     * the wheelEvent guard above. */
    if (isModalPanelOpen()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        /* No button held — passive movement, i.e. hover tracking (see
         * docs/adr/0030), not a drag. */
        scheduleHoverRequest(event->position().toPoint());
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (m_cursors.size() != 1) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    dismissHover();
    m_cursors[0] = offsetForPoint(event->position().toPoint());
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

void EditorViewport::leaveEvent(QEvent *event) {
    dismissHover();
    QWidget::leaveEvent(event);
}

void EditorViewport::focusOutEvent(QFocusEvent *event) {
    dismissCompletion();
    dismissHover();
    /* The caret's look depends on having focus, and nothing else would
     * ask for a repaint until something moved. */
    update();
    emit focusChanged(false);
    QWidget::focusOutEvent(event);
}

void EditorViewport::focusInEvent(QFocusEvent *event) {
    /* From full brightness, not wherever the fade happened to be when
     * focus left. */
    m_idleTicks = 0;
    m_caretVisible = true;
    update();
    emit focusChanged(true);
    QWidget::focusInEvent(event);
}
