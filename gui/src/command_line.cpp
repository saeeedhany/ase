#include "command_line.h"

#include "editor_viewport.h"
#include "letter_badge.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPalette>

CommandLine::CommandLine(EditorViewport *viewport) : FloatingPanel(viewport), m_viewport(viewport) {
    QWidget *content = contentWidget();
    auto *layout = new QHBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    m_badge = new LetterBadge(QLatin1Char(':'), content);
    layout->addWidget(m_badge);
    setDragHandle(m_badge); /* see docs/adr/0031 */
    m_edit = new QLineEdit(content);
    m_edit->setMinimumWidth(320);
    m_edit->setFrame(false);
    layout->addWidget(m_edit);

    m_edit->installEventFilter(this);
}

void CommandLine::openCommandLine() {
    refreshTheme();
    setAnimated(m_viewport->animationsEnabled());
    openPanel();
    m_edit->clear();
    m_edit->setFocus();
}

void CommandLine::hideBar() {
    setAnimated(m_viewport->animationsEnabled());
    closePanel();
    m_viewport->setFocus();
}

void CommandLine::restoreFocusAfterDrag() {
    m_edit->setFocus();
}

void CommandLine::refreshTheme() {
    setColors(m_viewport->panelBackgroundColor(), m_viewport->panelBorderColor());

    QColor badgeFill = m_viewport->textColor();
    badgeFill.setAlpha(220);
    m_badge->setColors(badgeFill, m_viewport->backgroundColor());

    QPalette editPalette = m_edit->palette();
    editPalette.setColor(QPalette::Base, m_viewport->panelFieldColor());
    editPalette.setColor(QPalette::Text, m_viewport->textColor());
    editPalette.setColor(QPalette::Highlight, m_viewport->panelBorderColor());
    editPalette.setColor(QPalette::HighlightedText, m_viewport->textColor());
    m_edit->setPalette(editPalette);
}

/* Return/Escape consumed here, not left to QLineEdit's native
 * returnPressed — see docs/adr/0024's note on FileBrowserPanel for why
 * that matters: hideBar() moves focus to the viewport synchronously,
 * mid-key-press-handling, and the native path can redeliver the same
 * key press to the newly-focused editor afterward. */
bool CommandLine::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && watched == m_edit) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideBar();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            QString command = m_edit->text();
            hideBar();
            m_viewport->runCommand(command);
            return true;
        }
    }
    return FloatingPanel::eventFilter(watched, event);
}
