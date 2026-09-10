#include "file_browser_panel.h"

#include "editor_viewport.h"
#include "letter_badge.h"
#include "scrollbar_style.h"
#include "smooth_scroll.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPropertyAnimation>
#include <QVBoxLayout>

namespace {
constexpr int kRowHighlightAnimMs = 85; /* fast — see docs/adr/0024, docs/adr/0027 */
}

FileBrowserPanel::FileBrowserPanel(EditorViewport *viewport) : FloatingPanel(viewport), m_viewport(viewport) {
    QWidget *content = contentWidget();
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);
    m_badge = new LetterBadge(QLatin1Char('O'), content);
    pathRow->addWidget(m_badge);
    setDragHandle(m_badge); /* see docs/adr/0031 */
    m_filterEdit = new QLineEdit(content);
    m_filterEdit->setMinimumWidth(480);
    m_filterEdit->setFrame(false);
    pathRow->addWidget(m_filterEdit);
    layout->addLayout(pathRow);

    m_listWidget = new QListWidget(content);
    m_listWidget->setMinimumHeight(260);
    m_listWidget->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_listWidget);
    installSmoothScroll(m_listWidget, m_viewport); /* see docs/adr/0031 */

    /* A plain flat bar, child of the list's viewport, sliding between
     * rows on an animation instead of relying on the native (instant,
     * OS-styled) selection rect — see docs/adr/0024. The native
     * highlight is made invisible in refreshTheme() (Highlight ==
     * Base) so the two never compete. */
    m_rowHighlight = new TranslucentBar(m_listWidget->viewport());
    m_rowHighlight->hide();
    m_rowHighlightAnim = new QPropertyAnimation(m_rowHighlight, "geometry", this);
    m_rowHighlightAnim->setDuration(kRowHighlightAnimMs);
    m_rowHighlightAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_filterEdit->installEventFilter(this);
    m_listWidget->installEventFilter(this);

    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &text) { applyFilter(text); });
    connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int row) { moveRowHighlight(row, true); });
    /* itemActivated stays for double-click only — keyboard Return is
     * handled directly in eventFilter, see its comment for why. */
    connect(m_listWidget, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { activateEntry(item->text()); });
}

void FileBrowserPanel::openFor(Mode mode) {
    m_mode = mode;
    refreshTheme();
    m_badge->setLetter(mode == Mode::Open ? QLatin1Char('O') : QLatin1Char('S'));

    /* Must happen before setDirectory() populates the list — see
     * FloatingPanel::revealForSetup()'s doc comment. */
    revealForSetup();

    QString filePath = m_viewport->filePath();
    QString startDir = filePath.isEmpty() ? QDir::currentPath() : QFileInfo(filePath).absolutePath();
    setDirectory(startDir);

    if (mode == Mode::SaveAs && !filePath.isEmpty()) {
        m_filterEdit->setText(QFileInfo(filePath).fileName());
    }

    setAnimated(m_viewport->animationsEnabled());
    openPanel();
    m_filterEdit->setFocus();
    m_filterEdit->selectAll();
}

void FileBrowserPanel::hideBar() {
    setAnimated(m_viewport->animationsEnabled());
    closePanel();
    m_viewport->setFocus();
}

void FileBrowserPanel::refreshTheme() {
    setColors(m_viewport->panelBackgroundColor(), m_viewport->panelBorderColor());

    QColor badgeFill = m_viewport->textColor();
    badgeFill.setAlpha(220);
    m_badge->setColors(badgeFill, m_viewport->backgroundColor());

    QPalette editPalette = m_filterEdit->palette();
    editPalette.setColor(QPalette::Base, m_viewport->panelFieldColor());
    editPalette.setColor(QPalette::Text, m_viewport->textColor());
    editPalette.setColor(QPalette::Highlight, m_viewport->panelBorderColor());
    editPalette.setColor(QPalette::HighlightedText, m_viewport->textColor());
    m_filterEdit->setPalette(editPalette);

    /* Highlight == Base so the native selection rect renders invisible
     * — the animated m_rowHighlight bar is the only visible highlight,
     * see the constructor's comment. */
    QPalette listPalette = m_listWidget->palette();
    listPalette.setColor(QPalette::Base, m_viewport->panelFieldColor());
    listPalette.setColor(QPalette::Text, m_viewport->textColor());
    listPalette.setColor(QPalette::Highlight, m_viewport->panelFieldColor());
    listPalette.setColor(QPalette::HighlightedText, m_viewport->textColor());
    m_listWidget->setPalette(listPalette);

    /* Reuses the exact translucent tone text selection already uses in
     * the editor itself — one consistent "this is highlighted" color
     * across the whole app, not a new one invented for this list. */
    m_rowHighlight->setColor(m_viewport->selectionColor());

    QColor handle = m_viewport->textColor();
    handle.setAlpha(90);
    QColor handleHover = m_viewport->textColor();
    handleHover.setAlpha(170);
    m_listWidget->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
}

/* Dotfiles excluded (no QDir::Hidden in the filter) and no toggle to
 * show them — v1 simplification, matching this project's tolerance for
 * a documented, undoable-later scope cut over a half-built option. */
void FileBrowserPanel::setDirectory(const QString &dir) {
    QDir directory(dir);
    if (!directory.exists()) {
        return;
    }
    m_currentDir = directory.absolutePath();
    QString label = QDir(m_currentDir).dirName();
    m_filterEdit->setPlaceholderText(label.isEmpty() ? QStringLiteral("/") : label);
    m_filterEdit->clear();

    m_listWidget->blockSignals(true);
    m_listWidget->clear();
    if (!directory.isRoot()) {
        m_listWidget->addItem(QStringLiteral(".."));
    }

    directory.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    directory.setSorting(QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    const QFileInfoList entries = directory.entryInfoList();
    for (const QFileInfo &info : entries) {
        m_listWidget->addItem(info.isDir() ? info.fileName() + QLatin1Char('/') : info.fileName());
    }
    /* A freshly populated (and, on first open, still-hidden) list
     * doesn't have real row geometry yet — Qt defers that layout pass.
     * Forcing it synchronously here, before setCurrentRow/scrollToTop/
     * visualRect all (correctly) depend on it, avoids a real bug this
     * had otherwise: the top row(s) rendering as scrolled out of view
     * in the very first open, and the highlight bar landing on a
     * garbage rect. See docs/adr/0024. */
    m_listWidget->doItemsLayout();
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
    m_listWidget->blockSignals(false);
    m_listWidget->scrollToTop();
    moveRowHighlight(m_listWidget->currentRow(), false);
}

void FileBrowserPanel::applyFilter(const QString &query) {
    QString needle = query.trimmed().toLower();
    int firstMatch = -1;
    for (int row = 0; row < m_listWidget->count(); ++row) {
        QListWidgetItem *item = m_listWidget->item(row);
        bool isUp = (item->text() == QLatin1String(".."));
        bool matches = isUp || needle.isEmpty() || item->text().toLower().contains(needle);
        item->setHidden(!matches);
        if (matches && !isUp && firstMatch < 0) {
            firstMatch = row;
        }
    }
    if (firstMatch < 0 && m_listWidget->count() > 0 && !m_listWidget->item(0)->isHidden()) {
        firstMatch = 0; /* nothing beyond ".." matched — land on ".." itself */
    }
    if (firstMatch >= 0) {
        m_listWidget->setCurrentRow(firstMatch);
    } else {
        moveRowHighlight(-1, false);
    }
}

void FileBrowserPanel::activateEntry(const QString &name) {
    if (name == QLatin1String("..")) {
        QDir parent(m_currentDir);
        parent.cdUp();
        setDirectory(parent.absolutePath());
        return;
    }

    bool isDirEntry = name.endsWith(QLatin1Char('/'));
    QString cleanName = isDirEntry ? name.chopped(1) : name;
    QString fullPath = QDir(m_currentDir).filePath(cleanName);

    if (isDirEntry) {
        setDirectory(fullPath);
        return;
    }
    if (m_mode == Mode::Open) {
        m_viewport->openFile(fullPath);
        hideBar();
    } else {
        /* Fills the field rather than saving immediately — a stray
         * double-click shouldn't silently overwrite a file. */
        m_filterEdit->setText(cleanName);
        m_filterEdit->setFocus();
    }
}

void FileBrowserPanel::confirmCurrent() {
    if (m_mode == Mode::Open) {
        QListWidgetItem *item = m_listWidget->currentItem();
        if (item == nullptr || item->isHidden()) {
            return;
        }
        activateEntry(item->text());
        return;
    }

    QString text = m_filterEdit->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    QString resolved = QDir::isAbsolutePath(text) ? text : QDir(m_currentDir).filePath(text);
    if (QFileInfo(resolved).isDir()) {
        setDirectory(resolved);
        return;
    }
    m_viewport->saveAs(resolved);
    hideBar();
}

int FileBrowserPanel::nextVisibleRow(int fromRow, int step) const {
    int count = m_listWidget->count();
    int row = fromRow;
    for (int i = 0; i < count; ++i) {
        row += step;
        if (row < 0 || row >= count) {
            return -1;
        }
        if (!m_listWidget->item(row)->isHidden()) {
            return row;
        }
    }
    return -1;
}

void FileBrowserPanel::moveRowHighlight(int row, bool animate) {
    m_rowHighlightAnim->stop();
    if (row < 0) {
        m_rowHighlight->hide();
        return;
    }
    QRect target = m_listWidget->visualRect(m_listWidget->model()->index(row, 0));
    if (!m_rowHighlight->isVisible() || !animate) {
        m_rowHighlight->setGeometry(target);
        m_rowHighlight->show();
        return;
    }
    m_rowHighlightAnim->setStartValue(m_rowHighlight->geometry());
    m_rowHighlightAnim->setEndValue(target);
    m_rowHighlightAnim->start();
}

/* Return is handled here for *both* widgets, not left to their native
 * Return handling (QLineEdit's returnPressed signal / QListWidget's own
 * Return-triggers-itemActivated) — an event filter runs *before* the
 * target's own handling, so returning true here fully consumes the key
 * press before Qt's native path ever sees it. This isn't stylistic:
 * relying on the native signals had a real, reproducible bug (see
 * docs/adr/0023) — activateEntry()/confirmCurrent() can end in
 * hideBar(), which moves keyboard focus to the viewport synchronously,
 * from inside the very key-press handling still in progress, and Qt's
 * native dispatch went on to redeliver that same key press to the
 * newly-focused EditorViewport afterward.
 *
 * Up/Down in the filter field are forwarded to the list (skipping
 * filtered-out rows) so typing-to-filter and arrow-to-pick compose the
 * way a quick-open/fuzzy-find field normally does — see docs/adr/0024. */
bool FileBrowserPanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && (watched == m_filterEdit || watched == m_listWidget)) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->key() == Qt::Key_Escape) {
            hideBar();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (watched == m_filterEdit) {
                confirmCurrent();
            } else {
                QListWidgetItem *item = m_listWidget->currentItem();
                if (item != nullptr) {
                    activateEntry(item->text());
                }
            }
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up) {
            /* Handled uniformly for both widgets (not just forwarded
             * from the filter field) so a filtered-out row is never
             * reachable via the keyboard from either one. */
            int step = (keyEvent->key() == Qt::Key_Down) ? 1 : -1;
            int next = nextVisibleRow(m_listWidget->currentRow(), step);
            if (next >= 0) {
                m_listWidget->setCurrentRow(next);
            }
            return true;
        }
    }
    return FloatingPanel::eventFilter(watched, event);
}
