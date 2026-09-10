#include "output_panel.h"

#include "editor_viewport.h"
#include "scrollbar_style.h"
#include "smooth_scroll.h"
#include "translucent_bar.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {
constexpr int kDividerWidth = 40;
constexpr int kDividerHeight = 2;
}

OutputPanel::OutputPanel(EditorViewport *viewport, QWidget *parent) : QWidget(parent), m_viewport(viewport) {
    /* Without this, the divider row below (and any other uncovered
     * pixel in this widget) shows Qt's default light-gray widget
     * background instead of the app's theme — a real bug found by
     * actually looking at a screenshot, not just confirming the
     * divider rendered. */
    setAutoFillBackground(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    /* Short and centered, not a full-width rule — see the class
     * comment and docs/adr/0027. */
    auto *dividerRow = new QWidget(this);
    auto *dividerLayout = new QHBoxLayout(dividerRow);
    dividerLayout->setContentsMargins(0, 6, 0, 6);
    dividerLayout->addStretch(1);
    m_divider = new TranslucentBar(dividerRow);
    m_divider->setFixedSize(kDividerWidth, kDividerHeight);
    dividerLayout->addWidget(m_divider);
    dividerLayout->addStretch(1);
    layout->addWidget(dividerRow);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setFrameShape(QFrame::NoFrame);
    m_text->setMinimumHeight(150);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_text);
    installSmoothScroll(m_text, m_viewport); /* see docs/adr/0031 */

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
    QPalette panelPal = palette();
    panelPal.setColor(QPalette::Window, m_viewport->backgroundColor());
    setPalette(panelPal);

    QPalette pal = m_text->palette();
    pal.setColor(QPalette::Base, m_viewport->backgroundColor());
    pal.setColor(QPalette::Text, m_viewport->textColor());
    m_text->setPalette(pal);

    m_divider->setColor(m_viewport->textColor());

    QColor handle = m_viewport->textColor();
    handle.setAlpha(90);
    QColor handleHover = m_viewport->textColor();
    handleHover.setAlpha(170);
    m_text->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
}
