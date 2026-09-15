#include "editor_viewport.h"

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

/* About and Open moved to Alt so Ctrl+O/Ctrl+I could go to the
 * jumplist, which has no alternative keys. */
bool EditorViewport::handleAltShortcut(QKeyEvent *event) {
    if (!(event->modifiers() & Qt::AltModifier) || (event->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    if (event->key() == Qt::Key_O) {
        if (m_fileBrowser != nullptr) {
            m_fileBrowser->openFor(FileBrowserPanel::Mode::Open);
        }
        return true;
    }
    if (event->key() == Qt::Key_I) {
        if (m_aboutPanel != nullptr) {
            m_aboutPanel->openAbout();
        }
        return true;
    }
    QWidget::keyPressEvent(event);
    return true;
}

bool EditorViewport::handleCtrlShortcut(QKeyEvent *event) {
    if (!(event->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    if (event->key() == Qt::Key_S) {
        /* Ctrl+Shift+S always opens Save-As, even with a path
         * already set — "save as" means "let me pick a
         * different one," not "save.". Ctrl+S with no path set
         * falls through to save()'s own Save-As fallback. */
        if (event->modifiers() & Qt::ShiftModifier) {
            if (m_fileBrowser != nullptr) {
                m_fileBrowser->openFor(FileBrowserPanel::Mode::SaveAs);
            }
        } else {
            save();
        }
        return true;
    }
    if (event->key() == Qt::Key_P) {
        /* Ctrl+P — open any file in the project by typing part
         * of its name, rather than walking there a directory at
         * a time. Same panel as Ctrl+O in a different mode; see
         * docs/adr/0065. */
        if (m_fileBrowser != nullptr) {
            m_fileBrowser->openFor(FileBrowserPanel::Mode::QuickOpen);
        }
        return true;
    }
    if (event->key() == Qt::Key_O && (event->modifiers() & Qt::ShiftModifier)) {
        /* Ctrl+Shift+O toggles the output panel directly,
         * without going through :output. Plain Ctrl+O is Vim's
         * jump-back now; Open moved to Alt+O (docs/adr/0068). */
        toggleOutputPanel();
        return true;
    }
    if (event->key() == Qt::Key_Semicolon) {
        /* Command-line trigger — Ctrl+; here, not a bare `:`
         * (that's the ex-command-line convention Vim mode will
         * use later; in normal mode a bare `:` has to stay a
         * literal, typeable character). See docs/adr/0025. */
        if (m_commandLine != nullptr) {
            m_commandLine->openPrompt(QLatin1Char(':'));
        }
        return true;
    }
    if (event->key() == Qt::Key_B) {
        compile();
        return true;
    }
    if (event->key() == Qt::Key_Q) {
        window()->close();
        return true;
    }
    if ((event->key() == Qt::Key_D || event->key() == Qt::Key_U) && vimModeActive() &&
        m_vimMode != VimMode::Insert) {
        /* The one place Vim mode *takes over* an existing
         * Ctrl shortcut rather than adding one. ADR 0046 set
         * out to keep the whole Ctrl chain mode-independent,
         * and that holds everywhere else — but Ctrl+D is
         * half-a-screen-down to anyone with vim in their
         * fingers, and having it fan out multi-cursors in
         * Normal mode is the kind of surprise that costs more
         * than the rule saves. Only in Normal/Visual: Insert
         * mode and `vim_mode = false` keep multi-cursor
         * Ctrl+D untouched, which is where multi-cursor
         * editing actually happens. See docs/adr/0059. */
        vimHalfPageMotion(event->key() == Qt::Key_D ? 1 : -1);
        return true;
    }
    if (event->key() == Qt::Key_D) {
        addCursorAtNextOccurrence();
        return true;
    }
    if (event->key() == Qt::Key_A) {
        selectAll();
        return true;
    }
    if (event->key() == Qt::Key_C) {
        copySelection();
        return true;
    }
    if (event->key() == Qt::Key_X) {
        cutSelection();
        return true;
    }
    if (event->key() == Qt::Key_V) {
        pasteClipboard();
        return true;
    }
    if (event->key() == Qt::Key_F) {
        if (m_findBar != nullptr) {
            /* Ctrl+Shift+F searches every file in the project;
             * Ctrl+F keeps its existing meaning, this buffer.
             * Same split as Ctrl+O / Ctrl+Shift+O above. */
            m_findBar->openFor((event->modifiers() & Qt::ShiftModifier) ? FindBar::Mode::Project
                                                                       : FindBar::Mode::Find);
        }
        return true;
    }
    if (event->key() == Qt::Key_H) {
        if (m_findBar != nullptr) {
            m_findBar->openFor(FindBar::Mode::Replace);
        }
        return true;
    }
    if (event->key() == Qt::Key_Z) {
        if (event->modifiers() & Qt::ShiftModifier) {
            redo();
        } else {
            undo();
        }
        return true;
    }
    if (event->key() == Qt::Key_Equal || event->key() == Qt::Key_Plus) {
        /* Ctrl+= (the unshifted key '+' shares on most layouts)
         * and Ctrl+Plus both zoom in, matching every other
         * app's convention (browsers, VS Code, ...). Live,
         * in-session only — see docs/adr/0050. */
        adjustFontSize(1);
        return true;
    }
    if (event->key() == Qt::Key_Minus) {
        adjustFontSize(-1);
        return true;
    }
    if (event->key() == Qt::Key_0) {
        resetFontSize();
        return true;
    }
    if (event->key() == Qt::Key_R && vimModeActive()) {
        /* Vim's own redo binding, additive to the existing
         * Ctrl+Shift+Z above — gated on vimModeActive() (not on
         * m_vimMode) so it works from Insert too, matching how
         * Ctrl+Z/Ctrl+Shift+Z are already mode-independent, and
         * so non-Vim users see no new shortcut. */
        redo();
        return true;
    }
    QWidget::keyPressEvent(event);
    return true;
}

void EditorViewport::keyPressEvent(QKeyEvent *event) {
    /* Belt-and-braces: a modal panel normally holds focus anyway, but
     * no focus-routing edge case may edit the document beneath. */
    if (isModalPanelOpen()) {
        QWidget::keyPressEvent(event);
        return;
    }

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
            /* Steps back a column, as vim does, unless already at 0. */
            if (m_cursors[0] > static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])) {
                moveCursorLeftAt(0, false);
            }
            m_vimMode = VimMode::Normal;
            vimEndInsertCapture();
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
    if (event->key() == Qt::Key_F1) {
        /* F1 is what people press without being told. */
        if (m_helpPanel != nullptr) {
            m_helpPanel->openHelp();
        }
        return;
    }
    if (event->key() == Qt::Key_F12) {
        /* Works in every mode; vim's `gd` is wired separately. */
        goToDefinition();
        return;
    }
    m_desiredColumn = -1;

    if (vimModeActive() && m_vimMode != VimMode::Insert) {
        if (handleVimNormalOrVisualKey(event)) {
            return;
        }
        /* Falls through only for keys Vim doesn't claim. */
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
        insertText(QByteArrayLiteral("    "));
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
        insertText(QByteArrayLiteral("\n"));
        break;
    default:
        if (handleAltShortcut(event)) {
            return;
        }
        if (handleCtrlShortcut(event)) {
            return;
        }

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
            insertText(text.toUtf8());
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
    QWidget::focusOutEvent(event);
}
