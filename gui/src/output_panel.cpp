#include "output_panel.h"

#include "editor_viewport.h"
#include "scrollbar_style.h"
#include "smooth_scroll.h"
#include "translucent_bar.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPlainTextEdit>
#include <QDir>
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

    /* One line above the results saying what was searched and how much
     * of the answer this is. Hidden in Output mode. */
    m_resultsHeader = new QLabel(this);
    m_resultsHeader->setContentsMargins(10, 0, 10, 6);
    m_resultsHeader->hide();
    layout->addWidget(m_resultsHeader);

    m_results = new QListWidget(this);
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setMinimumHeight(150);
    m_results->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_results->hide();
    layout->addWidget(m_results);
    installSmoothScroll(m_results, m_viewport);
    connect(m_results, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { activateRow(item); });
    /* Single click too: a results list is something you skim and poke
     * at, and requiring a double-click to follow a hit is the kind of
     * friction that makes people stop using the feature. */
    connect(m_results, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) { activateRow(item); });

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setFrameShape(QFrame::NoFrame);
    m_text->setMinimumHeight(150);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_text);
    installSmoothScroll(m_text, m_viewport); /* see docs/adr/0031 */

    hide(); /* until the first :compile — see the class comment */
}

void OutputPanel::setMode(Mode mode) {
    m_mode = mode;
    bool results = (mode == Mode::SearchResults);
    m_resultsHeader->setVisible(results);
    m_results->setVisible(results);
    m_text->setVisible(!results);
}

void OutputPanel::showSearchResults(const QString &root, const QString &needle,
                                     const project::SearchResult &result) {
    m_searchRoot = root;
    m_hits = result.hits;

    m_results->clear();
    for (const project::SearchHit &hit : m_hits) {
        /* path:line, then the line itself — the shape every compiler,
         * linter and grep on the machine already prints, so it reads
         * without being explained. */
        m_results->addItem(QStringLiteral("%1:%2:  %3").arg(hit.path).arg(hit.line).arg(hit.text));
    }

    QString summary;
    if (m_hits.isEmpty()) {
        summary = QStringLiteral("no matches for \"%1\" in %2 files").arg(needle).arg(result.filesSearched);
    } else {
        summary = QStringLiteral("%1 %2 for \"%3\" in %4 files")
                      .arg(m_hits.size())
                      .arg(m_hits.size() == 1 ? QStringLiteral("match") : QStringLiteral("matches"))
                      .arg(needle)
                      .arg(result.filesSearched);
        if (result.truncated) {
            /* Never quietly show a prefix of the answer as if it were
             * the answer. */
            summary = QStringLiteral("first ") + summary;
        }
    }
    m_resultsHeader->setText(summary);

    setMode(Mode::SearchResults);
    refreshTheme();
    show();
    if (!m_hits.isEmpty()) {
        m_results->setCurrentRow(0);
        /* Focus lands here so Up/Down/Enter work immediately — the list
         * is what you came to use. Escape hands focus back to the
         * editor (see keyPressEvent). */
        m_results->setFocus();
    }
}

void OutputPanel::activateRow(QListWidgetItem *item) {
    if (item == nullptr) {
        return;
    }
    int row = m_results->row(item);
    if (row < 0 || row >= m_hits.size()) {
        return;
    }
    const project::SearchHit &hit = m_hits[row];
    emit hitActivated(QDir(m_searchRoot).filePath(hit.path), hit.line);
}

void OutputPanel::appendLine(const QString &text) {
    setMode(Mode::Output);
    m_text->appendPlainText(text);
}

void OutputPanel::appendText(const QString &text) {
    setMode(Mode::Output);
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

/* Escape gives the editor its focus back without hiding the results —
 * you often want to look at the code and come back to the list. */
void OutputPanel::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && m_viewport != nullptr) {
        m_viewport->setFocus();
        return;
    }
    QWidget::keyPressEvent(event);
}

/* Plain background/text, not the translucent panel_background tone —
 * this is a docked part of the window, not floating chrome, so it
 * should read as an extension of the editor rather than an overlay. */
void OutputPanel::setViewport(EditorViewport *viewport) {
    if (m_viewport == viewport) {
        return;
    }
    m_viewport = viewport;
    refreshTheme();
}

void OutputPanel::refreshTheme() {
    if (m_viewport == nullptr) {
        return; /* constructed before any buffer exists — see setViewport */
    }
    QPalette panelPal = palette();
    panelPal.setColor(QPalette::Window, m_viewport->backgroundColor());
    setPalette(panelPal);

    QPalette pal = m_text->palette();
    pal.setColor(QPalette::Base, m_viewport->backgroundColor());
    pal.setColor(QPalette::Text, m_viewport->textColor());
    m_text->setPalette(pal);

    QPalette listPal = m_results->palette();
    listPal.setColor(QPalette::Base, m_viewport->backgroundColor());
    listPal.setColor(QPalette::Text, m_viewport->textColor());
    /* The native selection colour is a bright system blue — the one
     * thing in this app that would shout. Reuse the editor's own
     * selection tone. */
    listPal.setColor(QPalette::Highlight, m_viewport->selectionColor());
    listPal.setColor(QPalette::HighlightedText, m_viewport->textColor());
    m_results->setPalette(listPal);

    QPalette headerPal = m_resultsHeader->palette();
    QColor headerColor = m_viewport->textColor();
    headerColor.setAlpha(150); /* one tier down: it labels the list, it isn't the list */
    headerPal.setColor(QPalette::WindowText, headerColor);
    m_resultsHeader->setPalette(headerPal);

    m_divider->setColor(m_viewport->textColor());

    QColor handle = m_viewport->textColor();
    handle.setAlpha(90);
    QColor handleHover = m_viewport->textColor();
    handleHover.setAlpha(170);
    m_text->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
    m_results->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
}
