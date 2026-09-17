#include "output_panel.h"
#include <QVariantAnimation>
#include <QApplication>
#include <QAbstractItemView>
#include <QScrollBar>
#include <algorithm>
#include <QPropertyAnimation>
#include "motion.h"

#include "editor_viewport.h"
#include "scrollbar_style.h"
#include "smooth_scroll.h"
#include "list_navigation.h"
#include "panel_resize_handle.h"

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
/* Enough to read a few results; below this the panel is a sliver that
 * cannot say anything useful. */
constexpr int kMinPanelHeight = 90;
/* The editor must keep more of the window than the panel does — a
 * panel that can swallow the whole window is a panel you have to fight. */
constexpr int kMinEditorHeight = 120;
/* One keypress of resize: a visible step without being a jump. */
constexpr int kResizeStep = 60;
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

    /* A hairline across the full width, and the grip that resizes the
     * panel. It replaced a short centred bar: a seam runs the width of
     * what it separates, and a bar in the middle read as an object
     * sitting on the layout rather than as the join. See
     * docs/adr/0117. */
    m_handle = new PanelResizeHandle(this);
    layout->addWidget(m_handle);
    connect(m_handle, &PanelResizeHandle::dragged, this,
            [this](int delta) { resizeByDrag(delta); });

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
    m_results->installEventFilter(this);
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

/* The list itself, under a summary the caller writes. Search, find-
 * references and document symbols all produce "places in the project,
 * one per line, jump on Enter" and differ only in what to call them. */
void OutputPanel::showLocations(const QString &root, const QString &summary,
                                 const QVector<project::SearchHit> &hits) {
    m_searchRoot = root;
    m_hits = hits;

    m_results->clear();
    for (const project::SearchHit &hit : m_hits) {
        /* path:line, then the line itself — the shape every compiler,
         * linter and grep on the machine already prints, so it reads
         * without being explained. */
        m_results->addItem(QStringLiteral("%1:%2:  %3").arg(hit.path).arg(hit.line).arg(hit.text));
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

void OutputPanel::showSearchResults(const QString &root, const QString &needle,
                                     const project::SearchResult &result) {
    QString summary;
    if (result.hits.isEmpty()) {
        summary = QStringLiteral("no matches for \"%1\" in %2 files").arg(needle).arg(result.filesSearched);
    } else {
        summary = QStringLiteral("%1 %2 for \"%3\" in %4 files")
                      .arg(result.hits.size())
                      .arg(result.hits.size() == 1 ? QStringLiteral("match") : QStringLiteral("matches"))
                      .arg(needle)
                      .arg(result.filesSearched);
        if (result.truncated) {
            /* Never quietly show a prefix of the answer as if it were
             * the answer. */
            summary = QStringLiteral("first ") + summary;
        }
    }
    showLocations(root, summary, result.hits);
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

bool OutputPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_results && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape && m_viewport != nullptr) {
            m_viewport->setFocus();
            return true;
        }
        /* The arrows already work here (QListWidget handles them); this
         * is only about Ctrl+J/K, which it does not — see
         * gui/src/list_navigation.h. */
        if (keyEvent->modifiers() & Qt::ControlModifier) {
            if (int delta = listnav::delta(keyEvent); delta != 0) {
                int next = m_results->currentRow() + delta;
                if (next >= 0 && next < m_results->count()) {
                    selectRowSmoothly(next);
                }
                return true;
            }
        }
        /* The arrows work by themselves, but QListWidget snaps the view
         * when the selection leaves it. Handled here so a key and a
         * wheel scroll the same way — installSmoothScroll only sees
         * wheel events. */
        if (keyEvent->modifiers() == Qt::NoModifier &&
            (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up)) {
            int next = m_results->currentRow() + (keyEvent->key() == Qt::Key_Down ? 1 : -1);
            if (next >= 0 && next < m_results->count()) {
                selectRowSmoothly(next);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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

    m_handle->setColor(m_viewport->textColor());

    QColor handle = m_viewport->textColor();
    handle.setAlpha(90);
    QColor handleHover = m_viewport->textColor();
    handleHover.setAlpha(170);
    m_text->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
    m_results->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
}

/* ---- height (ADR 0117) ---- */

/* Dragging down shrinks a panel that lives below the seam. */
void OutputPanel::resizeByDrag(int delta) {
    applyHeight((m_chosenHeight > 0 ? m_chosenHeight : height()) - delta, false);
}

void OutputPanel::growBy(int delta) {
    if (!isVisible()) {
        return;
    }
    applyHeight((m_chosenHeight > 0 ? m_chosenHeight : height()) + delta, true);
}

void OutputPanel::applyHeight(int wanted, bool animated) {
    /* The editor above keeps its floor whatever the panel asks for.
     * Without this the panel can be dragged over the whole window and
     * there is nothing left to grab it back by. */
    int ceiling = kMinPanelHeight;
    if (parentWidget() != nullptr) {
        ceiling = std::max(kMinPanelHeight, parentWidget()->height() - kMinEditorHeight);
    }
    int target = std::clamp(wanted, kMinPanelHeight, ceiling);
    if (target == m_chosenHeight) {
        return;
    }
    m_chosenHeight = target;

    if (!animated || m_viewport == nullptr || !m_viewport->animationsEnabled()) {
        if (m_resize != nullptr) {
            m_resize->stop();
        }
        setFixedHeight(target);
        return;
    }

    /* One animation, reused. The first version made a new one per
     * keypress without stopping the last, so two presses in quick
     * succession left the older animation still running — and its
     * finished handler then set the panel back to *its* target, mid
     * flight. Pressing twice looked fine; pressing repeatedly lurched.
     *
     * It drives setFixedHeight per frame rather than animating
     * maximumHeight and fixing the height at the end, so there is no
     * moment where min and max disagree about what is happening. */
    if (m_resize == nullptr) {
        m_resize = new QVariantAnimation(this);
        m_resize->setEasingCurve(motion::kCurve);
        connect(m_resize, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) { setFixedHeight(value.toInt()); });
    }
    m_resize->stop();
    m_resize->setDuration(motion::kChrome);
    m_resize->setStartValue(height());
    m_resize->setEndValue(target);
    m_resize->start();
}

/* Focus lands on the list, which is the part you navigate. */
void OutputPanel::focusList() {
    if (m_results->isVisible()) {
        if (m_results->currentRow() < 0 && m_results->count() > 0) {
            m_results->setCurrentRow(0);
        }
        m_results->setFocus();
    } else {
        m_text->setFocus();
    }
}

void OutputPanel::setRegionActive(bool active) {
    m_handle->setRegionActive(active);
}

bool OutputPanel::hasFocusInside() const {
    QWidget *focused = QApplication::focusWidget();
    return focused != nullptr && (focused == this || isAncestorOf(focused));
}

/* Moves the selection and eases the view to follow, instead of the jump
 * QListWidget does when the current row leaves the viewport. The wheel
 * already glides (installSmoothScroll); this is the keyboard saying the
 * same thing. See docs/adr/0117. */
void OutputPanel::selectRowSmoothly(int row) {
    QScrollBar *bar = m_results->verticalScrollBar();
    int before = bar->value();

    /* Let the list work out where it wants to be, then put the scroll
     * back and ease to it — cheaper and more robust than computing the
     * target row's position by hand. */
    m_results->setCurrentRow(row);
    m_results->scrollTo(m_results->model()->index(row, 0), QAbstractItemView::EnsureVisible);
    int target = bar->value();
    if (target == before) {
        return;
    }
    if (m_viewport == nullptr || !m_viewport->animationsEnabled()) {
        return;
    }

    bar->setValue(before);
    if (m_scroll == nullptr) {
        m_scroll = new QPropertyAnimation(bar, "value", this);
    }
    m_scroll->stop();
    m_scroll->setStartValue(before);
    m_scroll->setEndValue(target);
    motion::apply(m_scroll, motion::kScroll);
    m_scroll->start();
}
