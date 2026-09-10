#include "output_panel.h"

#include "editor_viewport.h"

#include <QFontDatabase>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>

OutputPanel::OutputPanel(EditorViewport *viewport, QWidget *parent) : QWidget(parent), m_viewport(viewport) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setFrameShape(QFrame::NoFrame);
    m_text->setMinimumHeight(150);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_text);

    hide(); /* until the first :compile — see the class comment */
}

void OutputPanel::appendLine(const QString &text) {
    m_text->appendPlainText(text);
}

void OutputPanel::appendText(const QString &text) {
    /* QPlainTextEdit has no "append raw text, no forced newline"
     * convenience (appendPlainText always adds one) — move the cursor
     * to the end and insert directly. */
    QTextCursor cursor = m_text->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    m_text->setTextCursor(cursor);
    m_text->ensureCursorVisible();
}

void OutputPanel::clear() {
    m_text->clear();
}

/* Plain background/text, not the translucent panel_background tone —
 * this is a docked part of the window, not floating chrome, so it
 * should read as an extension of the editor rather than an overlay. */
void OutputPanel::refreshTheme() {
    QPalette pal = m_text->palette();
    pal.setColor(QPalette::Base, m_viewport->backgroundColor());
    pal.setColor(QPalette::Text, m_viewport->textColor());
    m_text->setPalette(pal);
}
