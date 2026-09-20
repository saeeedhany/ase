#include "find_bar.h"

#include "editor_viewport.h"
#include "letter_badge.h"
#include "smooth_line_edit.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPalette>
#include <QVBoxLayout>

FindBar::FindBar(EditorViewport *viewport) : FloatingPanel(viewport), m_viewport(viewport) {
    /* Top-right, not centered like every other panel — user feedback:
     * a centered find bar sits on top of the text you're actively
     * searching, which reads worse here than for a glance-act-dismiss
     * panel like Open/Save-As. See docs/adr/0026. */
    setAnchor(Anchor::TopRight);

    QWidget *content = contentWidget();
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *findRow = new QHBoxLayout();
    findRow->setSpacing(8);
    m_findBadge = new LetterBadge(QLatin1Char('F'), content);
    findRow->addWidget(m_findBadge);
    setDragHandle(m_findBadge); /* see docs/adr/0031 */
    m_findEdit = new SmoothLineEdit(content);
    m_findEdit->setMinimumWidth(240);
    findRow->addWidget(m_findEdit);
    layout->addLayout(findRow);

    m_replaceRow = new QWidget(content);
    auto *replaceRowLayout = new QHBoxLayout(m_replaceRow);
    replaceRowLayout->setContentsMargins(0, 0, 0, 0);
    replaceRowLayout->setSpacing(8);
    m_replaceBadge = new LetterBadge(QLatin1Char('R'), m_replaceRow);
    replaceRowLayout->addWidget(m_replaceBadge);
    m_replaceEdit = new SmoothLineEdit(m_replaceRow);
    replaceRowLayout->addWidget(m_replaceEdit);
    layout->addWidget(m_replaceRow);
    m_replaceRow->setVisible(false);

    m_findEdit->setFrame(false);
    m_replaceEdit->setFrame(false);
    m_findEdit->installEventFilter(this);
    m_replaceEdit->installEventFilter(this);

    connect(m_findEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        /* Project mode types a query for *another* search; highlighting
         * it in this buffer as you go would be a different answer to a
         * question nobody asked. */
        /* Incremental highlighting is for the buffer you are in; a
         * project search has not run yet and has nothing to highlight. */
        if (m_mode != Mode::Project && m_mode != Mode::ProjectReplace) {
            m_viewport->setFindQuery(text);
        }
    });
}

void FindBar::openFor(Mode mode) {
    m_mode = mode;
    refreshTheme();
    bool replaceMode = (mode == Mode::Replace || mode == Mode::ProjectReplace);
    m_replaceRow->setVisible(replaceMode);
    /* G for grep — the word everyone already has for this, and
     * unambiguous against F and R. */
    bool projectMode = (mode == Mode::Project || mode == Mode::ProjectReplace);
    m_findBadge->setLetter(projectMode ? QLatin1Char('G') : QLatin1Char('F'));

    /* Pre-fill from the current selection, single-line only — a
     * multi-line "needle" as a starting point is more surprising than
     * helpful. */
    QString selection = m_viewport->primarySelectionText();
    if (!selection.isEmpty() && !selection.contains(QLatin1Char('\n'))) {
        m_findEdit->setText(selection);
    }

    /* One jump per search, recorded where you *started* — incremental
     * search moves the cursor on every keystroke, and a jumplist entry
     * per keystroke would bury the position you actually want back. */
    m_viewport->recordJump();

    setAnimated(m_viewport->animationsEnabled());
    openPanel();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    if (!projectMode) {
        m_viewport->setFindQuery(m_findEdit->text());
    }
}

void FindBar::hideBar() {
    setAnimated(m_viewport->animationsEnabled());
    closePanel();
    /* Highlights go, the needle stays — `n` repeats whatever was last
     * searched for, however it was searched for. See docs/adr/0074. */
    m_viewport->clearFindHighlights();
    m_viewport->setFocus();
}

/* Whichever field is actually showing — see docs/adr/0044. */
void FindBar::restoreFocusAfterDrag() {
    (m_replaceRow->isVisible() ? m_replaceEdit : m_findEdit)->setFocus();
}

/* Badge/panel colors are all derived from EditorViewport's existing
 * config-driven theme (docs/adr/0022) — re-pulled every open, and also
 * on every config hot-reload (see EditorViewport::checkConfigReload),
 * so an edited config.ase takes effect on this panel exactly like it
 * already does for the editor itself. */
void FindBar::refreshTheme() {
    setColors(m_viewport->panelBackgroundColor(), m_viewport->panelBorderColor());

    QColor badgeFill = m_viewport->textColor();
    badgeFill.setAlpha(220);
    QColor badgeLetter = m_viewport->backgroundColor();
    m_findBadge->setColors(badgeFill, badgeLetter);
    m_replaceBadge->setColors(badgeFill, badgeLetter);

    /* QLineEdit's default palette is a bright native text-field style —
     * jarring against this app's dark, minimal palette. Full-contrast
     * text on a flat field the same shade as the panel itself, no
     * native frame (setFrame(false) above); the field's own background
     * sits slightly off the panel's via panelFieldColor so it still
     * reads as an input, without introducing a new hue. */
    /* One helper, so every field in the app is themed the same way and
     * gains any later fix at the same time — see docs/adr/0072. */
    m_findEdit->applyPanelTheme(m_viewport);
    m_replaceEdit->applyPanelTheme(m_viewport);
}

/* Return/Enter in the find field navigates (Shift = previous, wrapping
 * either direction); in the replace field it replaces the current
 * match, or all matches with Ctrl held. Escape closes the panel from
 * either field. No buttons — see docs/adr/0021 and docs/adr/0022. */
bool FindBar::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && (watched == m_findEdit || watched == m_replaceEdit)) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->key() == Qt::Key_Escape) {
            hideBar();
            return true;
        }

        bool isReturn = keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
        if (isReturn) {
            if (m_mode == Mode::ProjectReplace) {
                /* Enter from either field: both are filled in before
                 * anything runs, and Tab moves between them. */
                QString needle = m_findEdit->text();
                QByteArray replacement = m_replaceEdit->text().toUtf8();
                hideBar();
                m_viewport->replaceInProject(needle, replacement);
            } else if (watched == m_findEdit && m_mode == Mode::Project) {
                QString needle = m_findEdit->text();
                hideBar();
                m_viewport->searchProject(needle);
            } else if (watched == m_findEdit) {
                if (keyEvent->modifiers() & Qt::ShiftModifier) {
                    m_viewport->findPrevious();
                } else {
                    m_viewport->findNext();
                }
            } else if (keyEvent->modifiers() & Qt::ControlModifier) {
                m_viewport->replaceAllMatches(m_replaceEdit->text().toUtf8());
            } else {
                m_viewport->replaceCurrentMatch(m_replaceEdit->text().toUtf8());
            }
            return true;
        }
    }

    return FloatingPanel::eventFilter(watched, event);
}
