#include "help_panel.h"

#include "editor_viewport.h"
#include "letter_badge.h"
#include "scrollbar_style.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
/* Maintained by hand, not generated from the keybinding dispatch code
 * — see the class doc comment. Two-column rows via a plain HTML table;
 * no explicit colors in the markup so it inherits the QLabel's own
 * palette (refreshTheme() sets that), keeping this consistent with the
 * "one font color" pillar the rest of the app already follows. */
const char kHelpHtml[] =
    "<table cellspacing=\"4\" cellpadding=\"2\">"
    "<tr><td colspan=\"2\"><b>Navigation</b></td></tr>"
    "<tr><td>&larr; &rarr; &uarr; &darr;</td><td>Move cursor</td></tr>"
    "<tr><td>Shift + arrow</td><td>Extend selection</td></tr>"
    "<tr><td>Home / End</td><td>Line start / end</td></tr>"
    "<tr><td>Ctrl+D</td><td>Select next occurrence</td></tr>"
    "<tr><td colspan=\"2\">&nbsp;</td></tr>"

    "<tr><td colspan=\"2\"><b>Editing</b></td></tr>"
    "<tr><td>Backspace / Delete</td><td>Delete char or selection</td></tr>"
    "<tr><td>Ctrl+Z / Ctrl+Shift+Z</td><td>Undo / redo</td></tr>"
    "<tr><td>Ctrl+C / Ctrl+X / Ctrl+V</td><td>Copy / cut / paste</td></tr>"
    "<tr><td>Ctrl+A</td><td>Select all</td></tr>"
    "<tr><td colspan=\"2\">&nbsp;</td></tr>"

    "<tr><td colspan=\"2\"><b>Find &amp; replace</b></td></tr>"
    "<tr><td>Ctrl+F</td><td>Find</td></tr>"
    "<tr><td>Ctrl+H</td><td>Find &amp; replace</td></tr>"
    "<tr><td>Enter / Shift+Enter</td><td>Next / previous match</td></tr>"
    "<tr><td>Ctrl+Enter</td><td>Replace all (in the replace field)</td></tr>"
    "<tr><td colspan=\"2\">&nbsp;</td></tr>"

    "<tr><td colspan=\"2\"><b>Files</b></td></tr>"
    "<tr><td>Ctrl+O</td><td>Open</td></tr>"
    "<tr><td>Ctrl+S</td><td>Save</td></tr>"
    "<tr><td>Ctrl+Shift+S</td><td>Save as</td></tr>"
    "<tr><td colspan=\"2\">&nbsp;</td></tr>"

    "<tr><td colspan=\"2\"><b>Build</b></td></tr>"
    "<tr><td>Ctrl+B</td><td>Compile (build_command)</td></tr>"
    "<tr><td>Ctrl+Shift+O</td><td>Toggle output panel</td></tr>"
    "<tr><td colspan=\"2\">&nbsp;</td></tr>"

    "<tr><td colspan=\"2\"><b>Command line</b></td></tr>"
    "<tr><td>Ctrl+;</td><td>Open command line</td></tr>"
    "<tr><td>:w  :q  :compile  :output</td><td>Commands</td></tr>"
    "<tr><td colspan=\"2\">&nbsp;</td></tr>"

    "<tr><td colspan=\"2\"><b>Other</b></td></tr>"
    "<tr><td>Escape</td><td>Collapse selection, or close the open panel</td></tr>"
    "<tr><td>Ctrl+Q</td><td>Quit</td></tr>"
    "<tr><td>Ctrl+/</td><td>This help panel</td></tr>"
    "<tr><td>Ctrl+I</td><td>About</td></tr>"
    "</table>";
} // namespace

HelpPanel::HelpPanel(EditorViewport *viewport) : FloatingPanel(viewport), m_viewport(viewport) {
    QWidget *content = contentWidget();
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(8);
    m_badge = new LetterBadge(QLatin1Char('?'), content);
    headerRow->addWidget(m_badge);
    m_title = new QLabel(QStringLiteral("Keyboard shortcuts"), content);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    headerRow->addWidget(m_title);
    headerRow->addStretch(1);
    layout->addLayout(headerRow);

    m_body = new QLabel(QString::fromUtf8(kHelpHtml), content);
    m_body->setTextFormat(Qt::RichText);
    m_body->setWordWrap(true);

    m_scrollArea = new QScrollArea(content);
    m_scrollArea->setWidget(m_body);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setMinimumSize(480, 420);
    m_scrollArea->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_scrollArea);

    m_scrollArea->installEventFilter(this);
}

void HelpPanel::openHelp() {
    refreshTheme();
    setAnimated(m_viewport->animationsEnabled());
    openPanel();
    m_scrollArea->setFocus();
}

void HelpPanel::hideBar() {
    setAnimated(m_viewport->animationsEnabled());
    closePanel();
    m_viewport->setFocus();
}

void HelpPanel::refreshTheme() {
    setColors(m_viewport->panelBackgroundColor(), m_viewport->panelBorderColor());

    QColor badgeFill = m_viewport->textColor();
    badgeFill.setAlpha(220);
    m_badge->setColors(badgeFill, m_viewport->backgroundColor());

    /* QLabel's default palette text color doesn't follow this app's
     * custom dark theme at all — it rendered black regardless of
     * background, a real bug found by actually looking at a
     * screenshot. Every QLabel in this panel needs its palette set
     * explicitly; m_title was the one missed the first time around. */
    QPalette pal = m_title->palette();
    pal.setColor(QPalette::WindowText, m_viewport->textColor());
    m_title->setPalette(pal);
    m_body->setPalette(pal);

    QPalette scrollPal = m_scrollArea->palette();
    scrollPal.setColor(QPalette::Base, m_viewport->panelFieldColor());
    scrollPal.setColor(QPalette::Window, m_viewport->panelFieldColor());
    m_scrollArea->setPalette(scrollPal);
    m_scrollArea->setAutoFillBackground(true);
    m_body->setAutoFillBackground(false);

    QColor handle = m_viewport->textColor();
    handle.setAlpha(90);
    QColor handleHover = m_viewport->textColor();
    handleHover.setAlpha(170);
    m_scrollArea->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
}

bool HelpPanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && watched == m_scrollArea) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideBar();
            return true;
        }
    }
    return FloatingPanel::eventFilter(watched, event);
}
