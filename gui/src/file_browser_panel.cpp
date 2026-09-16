#include "file_browser_panel.h"

#include "themed_dialog.h"

#include "editor_viewport.h"
#include "fuzzy_match.h"
#include "letter_badge.h"
#include "list_navigation.h"
#include "project_files.h"
#include "smooth_line_edit.h"
#include "scrollbar_style.h"
#include "smooth_scroll.h"

#include <algorithm>

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

namespace {

/* A hard stop: this walks on the UI thread, and Ctrl+P in a home
 * directory must not freeze the editor. */
constexpr int kQuickOpenFileCap = 20000;

/* You never look past the first handful, and rebuilding thousands of
 * items per keystroke is real work. */
constexpr int kMaxQuickOpenRows = 200;

} // namespace

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

    /* The directory used to be a placeholder, which vanished as soon
     * as you typed — losing the context you needed while filtering. */
    m_pathLabel = new QLabel(content);
    m_pathLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    layout->addWidget(m_pathLabel);

    m_listWidget = new QListWidget(content);
    m_listWidget->setMinimumHeight(260);
    m_listWidget->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_listWidget);
    installSmoothScroll(m_listWidget, m_viewport); /* see docs/adr/0031 */

    /* Slides between rows instead of the native instant selection
     * rect, which refreshTheme() makes invisible so they don't
     * compete. */
    m_rowHighlight = new TranslucentBar(m_listWidget->viewport());
    m_rowHighlight->hide();
    m_rowHighlightAnim = new QPropertyAnimation(m_rowHighlight, "geometry", this);
    motion::apply(m_rowHighlightAnim, motion::kQuick);

    m_filterEdit->installEventFilter(this);
    m_listWidget->installEventFilter(this);

    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &text) { applyFilter(text); });
    connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int row) { moveRowHighlight(row, true); });
    /* Double-click only; Return is handled in eventFilter. */
    connect(m_listWidget, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { activateEntry(item->text()); });
}

void FileBrowserPanel::openFor(Mode mode) {
    m_mode = mode;
    refreshTheme();
    m_badge->setLetter((mode == Mode::Open)     ? QLatin1Char('O')
                       : (mode == Mode::SaveAs) ? QLatin1Char('S')
                                                : QLatin1Char('P'));

    /* Before setDirectory() populates the list — see revealForSetup(). */
    revealForSetup();

    QString filePath = m_viewport->filePath();
    QString startDir = filePath.isEmpty() ? QDir::currentPath() : QFileInfo(filePath).absolutePath();
    if (mode == Mode::QuickOpen) {
        setProjectRoot(startDir);
    } else {
        setDirectory(startDir);
    }

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

    m_filterEdit->applyPanelTheme(m_viewport);

    /* Highlight == Base hides the native rect, leaving m_rowHighlight
     * as the only visible one. */
    QPalette listPalette = m_listWidget->palette();
    listPalette.setColor(QPalette::Base, m_viewport->panelFieldColor());
    listPalette.setColor(QPalette::Text, m_viewport->textColor());
    listPalette.setColor(QPalette::Highlight, m_viewport->panelFieldColor());
    listPalette.setColor(QPalette::HighlightedText, m_viewport->textColor());
    m_listWidget->setPalette(listPalette);

    /* Derived from the text colour, not the editor's selection colour:
     * that one is tuned for the editor background and came out muddy
     * over the panel's lighter field. */
    QColor rowTint = m_viewport->textColor();
    rowTint.setAlpha(38);
    m_rowHighlight->setColor(rowTint);
    m_rowHighlight->setRadius(3.0);

    QColor handle = m_viewport->textColor();
    handle.setAlpha(90);
    QColor handleHover = m_viewport->textColor();
    handleHover.setAlpha(170);
    m_listWidget->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
    /* The default QLabel palette ignores the theme and renders black. */
    QColor pathColor = m_viewport->textColor();
    pathColor.setAlpha(140);
    QPalette pathPal = m_pathLabel->palette();
    pathPal.setColor(QPalette::WindowText, pathColor);
    m_pathLabel->setPalette(pathPal);

}

/* Dotfiles excluded, with no toggle — v1 simplification. */
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
    return confirmDestructive(this, m_viewport, QStringLiteral("Overwrite file"),
                              QStringLiteral("\"%1\" already exists. Overwrite it?")
                                  .arg(QFileInfo(path).fileName()),
                              QStringLiteral("Overwrite"));
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
    /* Qt defers row geometry, and setCurrentRow/scrollToTop/visualRect
     * all depend on it — without forcing it here the first open
     * rendered scrolled out of view with a garbage highlight rect. */
    m_listWidget->doItemsLayout();
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
    m_listWidget->blockSignals(false);
    m_listWidget->scrollToTop();
    moveRowHighlight(m_listWidget->currentRow(), false);
}

/* Every file in the project, relative to its root: the enclosing git
 * checkout if there is one, else the current directory. */
void FileBrowserPanel::setProjectRoot(const QString &startDir) {
    m_currentDir = project::rootFor(startDir);
    m_projectFiles = project::collect(m_currentDir, kQuickOpenFileCap, &m_projectFilesTruncated);

    QString label = QDir(m_currentDir).dirName();
    m_filterEdit->setPlaceholderText(label.isEmpty() ? QStringLiteral("/") : label);
    m_filterEdit->clear();

    QString shown = m_currentDir;
    QString home = QDir::homePath();
    if (shown == home || shown.startsWith(home + QLatin1Char('/'))) {
        shown = QLatin1Char('~') + shown.mid(home.size());
    }
    /* Admits when the walk was cut short: a listing that silently
     * stops at a cap lies about what you can open. */
    m_pathLabel->setText(m_projectFilesTruncated
                             ? QStringLiteral("%1 — first %2 files").arg(shown).arg(m_projectFiles.size())
                             : QStringLiteral("%1 — %2 files").arg(shown).arg(m_projectFiles.size()));

    applyQuickOpenFilter(QString());
}

/* Unlike applyFilter(), this reorders — a fuzzy finder's whole value
 * is that the file you meant is first. */
void FileBrowserPanel::applyQuickOpenFilter(const QString &query) {
    QString needle = query.trimmed();
    needle.remove(QLatin1Char(' '));

    QVector<QPair<int, const QString *>> scored;
    scored.reserve(m_projectFiles.size());
    for (const QString &candidate : m_projectFiles) {
        int score = 0;
        if (fuzzy::match(needle, candidate, &score)) {
            scored.push_back({score, &candidate});
        }
    }
    /* Stable, so equal scores don't shuffle as you type. */
    std::stable_sort(scored.begin(), scored.end(),
                     [](const QPair<int, const QString *> &a, const QPair<int, const QString *> &b) {
                         return a.first > b.first;
                     });

    m_listWidget->clear();
    int shown = std::min(static_cast<int>(scored.size()), kMaxQuickOpenRows);
    for (int i = 0; i < shown; ++i) {
        m_listWidget->addItem(*scored[i].second);
    }
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    } else {
        moveRowHighlight(-1, false);
    }
}

void FileBrowserPanel::applyFilter(const QString &query) {
    if (m_mode == Mode::QuickOpen) {
        applyQuickOpenFilter(query);
        return;
    }
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
    if (m_mode == Mode::QuickOpen) {
        /* Every entry is a file; there is nothing to navigate into. */
        m_viewport->requestOpenFile(QDir(m_currentDir).filePath(name));
        hideBar();
        return;
    }
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
        /* Fills the field: a stray double-click must not overwrite. */
        m_filterEdit->setText(cleanName);
        m_filterEdit->setFocus();
    }
}

void FileBrowserPanel::confirmCurrent() {
    if (m_mode == Mode::QuickOpen) {
        /* Here the field is a query, never a path. */
        QListWidgetItem *item = m_listWidget->currentItem();
        if (item != nullptr) {
            activateEntry(item->text());
        }
        return;
    }

    QString typed = m_filterEdit->text().trimmed();

    /* A typed path wins over the list selection in both modes. */
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
            /* Opening a not-yet-existing file by name is normal. */
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
            /* Nothing matched: treat it as a new file name here. */
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
    /* The one genuinely dangerous thing this panel can do. */
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

/* Return is consumed here for both widgets rather than via their
 * native signals: activating can end in hideBar(), which moves focus
 * to the viewport synchronously, and Qt then redelivered the same key
 * press to it. An event filter runs first, so returning true stops
 * that. See docs/adr/0023.
 *
 * Up/Down in the filter field forward to the list, skipping filtered-
 * out rows. */
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
        if (int step = listnav::delta(keyEvent); step != 0) {
            /* Both widgets, so a filtered-out row is never reachable. */
            int next = nextVisibleRow(m_listWidget->currentRow(), step);
            if (next >= 0) {
                m_listWidget->setCurrentRow(next);
            }
            return true;
        }
    }
    return FloatingPanel::eventFilter(watched, event);
}
