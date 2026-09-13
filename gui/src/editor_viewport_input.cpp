#include "editor_viewport.h"

#include "about_panel.h"
#include "command_line.h"
#include "completion_popup.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "help_panel.h"

#include <algorithm>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

void EditorViewport::keyPressEvent(QKeyEvent *event) {
    /* A modal panel already has keyboard focus in the normal case, so
     * this doesn't usually even get reached while one is open — this
     * is the defense-in-depth half of docs/adr/0044's isolation fix,
     * guaranteeing no keystroke edits the document underneath
     * regardless of any focus-routing edge case. */
    if (isModalPanelOpen()) {
        QWidget::keyPressEvent(event);
        return;
    }

    resetCaretBlink();
    dismissHover();

    /* Completion popup interception — see docs/adr/0030. Takes priority
     * over the Up/Down/Escape handling below and over Key_Return's
     * normal "insert a newline" case further down; any other key
     * (including plain typing) falls through to the normal handling,
     * which itself retriggers a fresh completion request via
     * refreshCache(). */
    if (m_completionPopup != nullptr && m_completionPopup->isShowingPopup()) {
        switch (event->key()) {
        case Qt::Key_Up:
            m_completionPopup->moveSelection(-1);
            return;
        case Qt::Key_Down:
            m_completionPopup->moveSelection(1);
            return;
        case Qt::Key_Escape:
            dismissCompletion();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            acceptCompletion();
            return;
        default:
            break;
        }
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
            /* Leaving Insert steps back one column, same as real vim —
             * only when not already at column 0, so Escape on an empty
             * or just-Home'd line doesn't try to move left of it. */
            if (m_cursors[0] > static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])) {
                moveCursorLeftAt(0, false);
            }
            m_vimMode = VimMode::Normal;
            resetVimPendingState();
        } else if (vimModeActive()) {
            /* From Normal or Visual: drop any pending operator/count and
             * any Visual-mode selection, stay/return to Normal. */
            resetVimPendingState();
            collapseToOneCursor();
            m_vimMode = VimMode::Normal;
            m_vimVisualLinewise = false;
        } else {
            collapseToOneCursor();
        }
        ensureCursorVisible();
        update();
        return;
    }
    if (event->key() == Qt::Key_F1) {
        /* The help key everywhere. It used to be Ctrl+/, which was a
         * fine mnemonic and the wrong key: F1 is what people press
         * without being told, and moving it also freed a Ctrl slot.
         * See docs/adr/0068. */
        if (m_helpPanel != nullptr) {
            m_helpPanel->openHelp();
        }
        return;
    }
    if (event->key() == Qt::Key_F12) {
        /* The universal editor binding for this, working in every mode
         * and whether or not Vim mode is on. Vim's own `gd` is wired in
         * the Normal/Visual dispatch. See docs/adr/0067. */
        goToDefinition();
        return;
    }
    m_desiredColumn = -1;

    if (vimModeActive() && m_vimMode != VimMode::Insert) {
        if (handleVimNormalOrVisualKey(event)) {
            return;
        }
        /* Falls through only for keys Vim doesn't claim (arrows, Home/
         * End, every Ctrl/Alt/Meta combo) — the switch/Ctrl-chain below
         * handles those exactly as it does with Vim mode off. */
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
        deleteBackward();
        break;
    case Qt::Key_Delete:
        /* Real vim gives Delete no default Normal-mode meaning either —
         * see docs/adr/0046. Reachable here at all only because Delete
         * isn't part of Vim's own recognized key set (handleVimNormal-
         * OrVisualKey returns false for it), by design. */
        if (vimModeActive() && m_vimMode != VimMode::Insert) {
            break;
        }
        deleteForward();
        break;
    case Qt::Key_Tab:
        /* Qt::Key_Tab's event->text() is "\t", a control character —
         * QChar::isPrint() is false for it, so it fell through to the
         * default branch below and out to QWidget::keyPressEvent
         * (Qt's default focus-traversal handling), meaning it did
         * nothing at all: a real, reported gap, not a deliberate
         * omission. Inserts 4 spaces, not a literal tab byte:
         * drawLine/xForColumn measure each run with plain
         * QFontMetrics::horizontalAdvance (no QTextLayout, no tab-stop
         * expansion), so a raw '\t' would measure at ~0 width and
         * render as an invisible non-indent — a soft tab renders
         * correctly with the exact same per-glyph measurement every
         * other character already uses, and is simplicity keeping with
         * the byte-level column model (docs/adr/0012) rather than
         * adding tab-stop-aware rendering for one key. Completion-popup
         * Tab-to-accept is intercepted earlier in this function and
         * never reaches here. See docs/adr/0031. Real vim gives Tab no
         * default Normal-mode meaning either — see docs/adr/0046 — so
         * this is skipped while Vim mode is on and not in Insert. */
        if (vimModeActive() && m_vimMode != VimMode::Insert) {
            break;
        }
        insertText(QByteArrayLiteral("    "));
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        /* Real vim gives Enter no default Normal-mode meaning either —
         * see docs/adr/0046 — so this is skipped while Vim mode is on
         * and not in Insert. */
        if (vimModeActive() && m_vimMode != VimMode::Insert) {
            break;
        }
        insertText(QByteArrayLiteral("\n"));
        break;
    default:
        /* Alt is where the two panels that had to move went — About and
         * Open — so Ctrl+O and Ctrl+I could go to Vim's jumplist, which
         * has no alternative keys and is used constantly. A panel opened
         * a few times a session can afford an unusual binding; a
         * navigation key cannot. See docs/adr/0068. */
        if ((event->modifiers() & Qt::AltModifier) && !(event->modifiers() & Qt::ControlModifier)) {
            if (event->key() == Qt::Key_O) {
                if (m_fileBrowser != nullptr) {
                    m_fileBrowser->openFor(FileBrowserPanel::Mode::Open);
                }
                return;
            }
            if (event->key() == Qt::Key_I) {
                if (m_aboutPanel != nullptr) {
                    m_aboutPanel->openAbout();
                }
                return;
            }
            QWidget::keyPressEvent(event);
            return;
        }
        if (event->modifiers() & Qt::ControlModifier) {
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
                return;
            }
            if (event->key() == Qt::Key_P) {
                /* Ctrl+P — open any file in the project by typing part
                 * of its name, rather than walking there a directory at
                 * a time. Same panel as Ctrl+O in a different mode; see
                 * docs/adr/0065. */
                if (m_fileBrowser != nullptr) {
                    m_fileBrowser->openFor(FileBrowserPanel::Mode::QuickOpen);
                }
                return;
            }
            if (event->key() == Qt::Key_O && (event->modifiers() & Qt::ShiftModifier)) {
                /* Ctrl+Shift+O toggles the output panel directly,
                 * without going through :output. Plain Ctrl+O is Vim's
                 * jump-back now; Open moved to Alt+O (docs/adr/0068). */
                toggleOutputPanel();
                return;
            }
            if (event->key() == Qt::Key_Semicolon) {
                /* Command-line trigger — Ctrl+; here, not a bare `:`
                 * (that's the ex-command-line convention Vim mode will
                 * use later; in normal mode a bare `:` has to stay a
                 * literal, typeable character). See docs/adr/0025. */
                if (m_commandLine != nullptr) {
                    m_commandLine->openCommandLine();
                }
                return;
            }
            if (event->key() == Qt::Key_B) {
                compile();
                return;
            }
            if (event->key() == Qt::Key_Q) {
                window()->close();
                return;
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
                return;
            }
            if (event->key() == Qt::Key_D) {
                addCursorAtNextOccurrence();
                return;
            }
            if (event->key() == Qt::Key_A) {
                selectAll();
                return;
            }
            if (event->key() == Qt::Key_C) {
                copySelection();
                return;
            }
            if (event->key() == Qt::Key_X) {
                cutSelection();
                return;
            }
            if (event->key() == Qt::Key_V) {
                pasteClipboard();
                return;
            }
            if (event->key() == Qt::Key_F) {
                if (m_findBar != nullptr) {
                    /* Ctrl+Shift+F searches every file in the project;
                     * Ctrl+F keeps its existing meaning, this buffer.
                     * Same split as Ctrl+O / Ctrl+Shift+O above. */
                    m_findBar->openFor((event->modifiers() & Qt::ShiftModifier) ? FindBar::Mode::Project
                                                                               : FindBar::Mode::Find);
                }
                return;
            }
            if (event->key() == Qt::Key_H) {
                if (m_findBar != nullptr) {
                    m_findBar->openFor(FindBar::Mode::Replace);
                }
                return;
            }
            if (event->key() == Qt::Key_Z) {
                if (event->modifiers() & Qt::ShiftModifier) {
                    redo();
                } else {
                    undo();
                }
                return;
            }
            if (event->key() == Qt::Key_Equal || event->key() == Qt::Key_Plus) {
                /* Ctrl+= (the unshifted key '+' shares on most layouts)
                 * and Ctrl+Plus both zoom in, matching every other
                 * app's convention (browsers, VS Code, ...). Live,
                 * in-session only — see docs/adr/0050. */
                adjustFontSize(1);
                return;
            }
            if (event->key() == Qt::Key_Minus) {
                adjustFontSize(-1);
                return;
            }
            if (event->key() == Qt::Key_0) {
                resetFontSize();
                return;
            }
            if (event->key() == Qt::Key_R && vimModeActive()) {
                /* Vim's own redo binding, additive to the existing
                 * Ctrl+Shift+Z above — gated on vimModeActive() (not on
                 * m_vimMode) so it works from Insert too, matching how
                 * Ctrl+Z/Ctrl+Shift+Z are already mode-independent, and
                 * so non-Vim users see no new shortcut. */
                redo();
                return;
            }
            QWidget::keyPressEvent(event);
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
