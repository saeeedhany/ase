#include "file_browser_panel.h"

#include "editor_viewport.h"
#include "letter_badge.h"
#include "smooth_line_edit.h"
#include "scrollbar_style.h"
#include "smooth_scroll.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPropertyAnimation>

#include "motion.h"
#include <QVBoxLayout>

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
    m_filterEdit = new SmoothLineEdit(content);
    m_filterEdit->setMinimumWidth(480);
    m_filterEdit->setFrame(false);
    pathRow->addWidget(m_filterEdit);
    layout->addLayout(pathRow);

    /* Every familiar file dialog tells you which directory you are in.
     * This one only ever showed the directory's *name* as a
     * placeholder, which vanished the moment you typed — so while
     * filtering, the one piece of context you needed was gone. See
     * docs/adr/0055. */
    m_pathLabel = new QLabel(content);
    m_pathLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    layout->addWidget(m_pathLabel);

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
    motion::apply(m_rowHighlightAnim, motion::kQuick);

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

void FileBrowserPanel::restoreFocusAfterDrag() {
    m_filterEdit->setFocus();
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
    /* Qt derives PlaceholderText from Text when it isn't set, and that
     * derivation lands almost black against this theme's dark field —
     * the placeholder was effectively invisible. Set explicitly, one
     * opacity tier down from real input. */
    QColor placeholder = m_viewport->textColor();
    placeholder.setAlpha(115);
    editPalette.setColor(QPalette::PlaceholderText, placeholder);
    m_filterEdit->setPalette(editPalette);
    /* The caret glides and breathes like the editor's own, and goes
     * back to a hard blink when `animations = false`. See
     * docs/adr/0058. */
    m_filterEdit->setAnimated(m_viewport->animationsEnabled());

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
    /* Derived from the text color rather than reusing the editor's
     * selection color: that one is tuned for text sitting on the editor
     * background, and over the panel's lighter field it came out muddy
     * rather than lit. Low alpha so the row's text stays the brightest
     * thing in it. */
    QColor rowTint = m_viewport->textColor();
    rowTint.setAlpha(38);
    m_rowHighlight->setColor(rowTint);
    m_rowHighlight->setRadius(3.0);

    QColor handle = m_viewport->textColor();
    handle.setAlpha(90);
    QColor handleHover = m_viewport->textColor();
    handleHover.setAlpha(170);
    m_listWidget->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
    /* Same trap every other QLabel in this app hit: the default palette
     * ignores the theme and renders black. One tier down, since the path
     * is context, not the thing you are acting on. */
    QColor pathColor = m_viewport->textColor();
    pathColor.setAlpha(140);
    QPalette pathPal = m_pathLabel->palette();
    pathPal.setColor(QPalette::WindowText, pathColor);
    m_pathLabel->setPalette(pathPal);

}

/* Dotfiles excluded (no QDir::Hidden in the filter) and no toggle to
 * show them — v1 simplification, matching this project's tolerance for
 * a documented, undoable-later scope cut over a half-built option. */
QString FileBrowserPanel::expandUser(const QString &path) {
    if (path == QLatin1String("~")) {
        return QDir::homePath();
    }
    if (path.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    return path;
}

bool FileBrowserPanel::looksLikePath(const QString &text) {
    return text.startsWith(QLatin1Char('~')) || QDir::isAbsolutePath(text) || text.contains(QLatin1Char('/'));
}

bool FileBrowserPanel::confirmOverwrite(const QString &path) {
    QColor bg = m_viewport->panelBackgroundColor();
    QColor border = m_viewport->panelBorderColor();
    QColor text = m_viewport->textColor();

    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setWindowTitle(QStringLiteral("Overwrite file"));
    box.setText(QStringLiteral("\"%1\" already exists. Overwrite it?").arg(QFileInfo(path).fileName()));
    QPushButton *overwriteButton = box.addButton(QStringLiteral("Overwrite"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancelButton);
    box.setStyleSheet(QStringLiteral("QMessageBox { background-color: %1; }"
                                      "QMessageBox QLabel { color: %2; }"
                                      "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                                      "padding: 4px 14px; min-width: 60px; }"
                                      "QPushButton:hover, QPushButton:default { border-color: %2; }")
                           .arg(bg.name(), text.name(), border.name()));
    box.exec();
    return box.clickedButton() == overwriteButton;
}

void FileBrowserPanel::setDirectory(const QString &dir) {
    QDir directory(dir);
    if (!directory.exists()) {
        return;
    }
    m_currentDir = directory.absolutePath();
    QString label = QDir(m_currentDir).dirName();
    m_filterEdit->setPlaceholderText(label.isEmpty() ? QStringLiteral("/") : label);
    m_filterEdit->clear();

    QString shown = m_currentDir;
    QString home = QDir::homePath();
    if (shown == home || shown.startsWith(home + QLatin1Char('/'))) {
        shown = QLatin1Char('~') + shown.mid(home.size());
    }
    m_pathLabel->setText(shown);

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
        m_viewport->requestOpenFile(fullPath);
        hideBar();
    } else {
        /* Fills the field rather than saving immediately — a stray
         * double-click shouldn't silently overwrite a file. */
        m_filterEdit->setText(cleanName);
        m_filterEdit->setFocus();
    }
}

void FileBrowserPanel::confirmCurrent() {
    QString typed = m_filterEdit->text().trimmed();

    /* A typed path wins over the list selection in *both* modes. This
     * used to be Save-As-only: in Open, typing a path just filtered the
     * listing to nothing and Enter did something unrelated, which is
     * the main reason this panel felt unfamiliar. See docs/adr/0055. */
    if (looksLikePath(typed)) {
        QString resolved = expandUser(typed);
        if (!QDir::isAbsolutePath(resolved)) {
            resolved = QDir(m_currentDir).filePath(resolved);
        }
        resolved = QDir::cleanPath(resolved);
        if (QFileInfo(resolved).isDir()) {
            setDirectory(resolved);
            return;
        }
        if (m_mode == Mode::Open) {
            /* A path that doesn't exist yet is fine — opening a new file
             * by name is a normal editor action (docs/adr/0006). */
            m_viewport->requestOpenFile(resolved);
            hideBar();
            return;
        }
        if (QFileInfo::exists(resolved) && !confirmOverwrite(resolved)) {
            return;
        }
        m_viewport->saveAs(resolved);
        hideBar();
        return;
    }

    if (m_mode == Mode::Open) {
        QListWidgetItem *item = m_listWidget->currentItem();
        if (item == nullptr || item->isHidden()) {
            /* Nothing matched the filter — treat what was typed as a new
             * file name in this directory rather than doing nothing at
             * all, which is what it used to do. */
            if (!typed.isEmpty()) {
                m_viewport->requestOpenFile(QDir(m_currentDir).filePath(typed));
                hideBar();
            }
            return;
        }
        activateEntry(item->text());
        return;
    }

    if (typed.isEmpty()) {
        return;
    }
    QString resolved = QDir(m_currentDir).filePath(typed);
    if (QFileInfo(resolved).isDir()) {
        setDirectory(resolved);
        return;
    }
    /* Silently clobbering an existing file is the one genuinely
     * dangerous thing this panel could do, and it did it. */
    if (QFileInfo::exists(resolved) && !confirmOverwrite(resolved)) {
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
