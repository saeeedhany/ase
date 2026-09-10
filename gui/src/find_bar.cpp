#include "find_bar.h"

#include "editor_viewport.h"
#include "letter_badge.h"

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
    m_findEdit = new QLineEdit(content);
    m_findEdit->setMinimumWidth(240);
    findRow->addWidget(m_findEdit);
    layout->addLayout(findRow);

    m_replaceRow = new QWidget(content);
    auto *replaceRowLayout = new QHBoxLayout(m_replaceRow);
    replaceRowLayout->setContentsMargins(0, 0, 0, 0);
    replaceRowLayout->setSpacing(8);
    m_replaceBadge = new LetterBadge(QLatin1Char('R'), m_replaceRow);
    replaceRowLayout->addWidget(m_replaceBadge);
    m_replaceEdit = new QLineEdit(m_replaceRow);
    replaceRowLayout->addWidget(m_replaceEdit);
    layout->addWidget(m_replaceRow);
    m_replaceRow->setVisible(false);

    m_findEdit->setFrame(false);
    m_replaceEdit->setFrame(false);
    m_findEdit->installEventFilter(this);
    m_replaceEdit->installEventFilter(this);

    connect(m_findEdit, &QLineEdit::textChanged, this,
            [this](const QString &text) { m_viewport->setFindQuery(text); });
}

void FindBar::openFor(Mode mode) {
    refreshTheme();
    bool replaceMode = (mode == Mode::Replace);
    m_replaceRow->setVisible(replaceMode);

    /* Pre-fill from the current selection, single-line only — a
     * multi-line "needle" as a starting point is more surprising than
     * helpful. */
    QString selection = m_viewport->primarySelectionText();
    if (!selection.isEmpty() && !selection.contains(QLatin1Char('\n'))) {
        m_findEdit->setText(selection);
    }

    setAnimated(m_viewport->animationsEnabled());
    openPanel();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    m_viewport->setFindQuery(m_findEdit->text());
}

void FindBar::hideBar() {
    setAnimated(m_viewport->animationsEnabled());
    closePanel();
    m_viewport->clearFindQuery();
    m_viewport->setFocus();
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
    QPalette editPalette = m_findEdit->palette();
    editPalette.setColor(QPalette::Base, m_viewport->panelFieldColor());
    editPalette.setColor(QPalette::Text, m_viewport->textColor());
    editPalette.setColor(QPalette::Highlight, m_viewport->panelBorderColor());
    editPalette.setColor(QPalette::HighlightedText, m_viewport->textColor());
    m_findEdit->setPalette(editPalette);
    m_replaceEdit->setPalette(editPalette);
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
            if (watched == m_findEdit) {
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
