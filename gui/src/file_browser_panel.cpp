#include "file_browser_panel.h"

#include "editor_viewport.h"
#include "letter_badge.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QVBoxLayout>

FileBrowserPanel::FileBrowserPanel(EditorViewport *viewport) : FloatingPanel(viewport), m_viewport(viewport) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);
    m_badge = new LetterBadge(QLatin1Char('O'), this);
    pathRow->addWidget(m_badge);
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setMinimumWidth(480);
    m_pathEdit->setFrame(false);
    pathRow->addWidget(m_pathEdit);
    layout->addLayout(pathRow);

    m_listWidget = new QListWidget(this);
    m_listWidget->setMinimumHeight(260);
    m_listWidget->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_listWidget);

    m_pathEdit->installEventFilter(this);
    m_listWidget->installEventFilter(this);

    /* itemActivated stays for double-click only — see eventFilter's
     * comment for why keyboard Return is handled there instead, for
     * both widgets. */
    connect(m_listWidget, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { activateEntry(item->text()); });
}

void FileBrowserPanel::openFor(Mode mode) {
    m_mode = mode;
    refreshTheme();
    m_badge->setLetter(mode == Mode::Open ? QLatin1Char('O') : QLatin1Char('S'));

    QString filePath = m_viewport->filePath();
    QString startDir = filePath.isEmpty() ? QDir::currentPath() : QFileInfo(filePath).absolutePath();
    setDirectory(startDir);

    if (mode == Mode::SaveAs && !filePath.isEmpty()) {
        m_pathEdit->setText(QDir(m_currentDir).filePath(QFileInfo(filePath).fileName()));
    }

    setAnimated(m_viewport->animationsEnabled());
    openPanel();
    m_pathEdit->setFocus();
    m_pathEdit->selectAll();
}

void FileBrowserPanel::hideBar() {
    closePanel();
    m_viewport->setFocus();
}

void FileBrowserPanel::refreshTheme() {
    setColors(m_viewport->panelBackgroundColor(), m_viewport->panelBorderColor());

    QColor badgeFill = m_viewport->textColor();
    badgeFill.setAlpha(220);
    m_badge->setColors(badgeFill, m_viewport->backgroundColor());

    QPalette editPalette = m_pathEdit->palette();
    editPalette.setColor(QPalette::Base, m_viewport->panelFieldColor());
    editPalette.setColor(QPalette::Text, m_viewport->textColor());
    editPalette.setColor(QPalette::Highlight, m_viewport->panelBorderColor());
    editPalette.setColor(QPalette::HighlightedText, m_viewport->textColor());
    m_pathEdit->setPalette(editPalette);

    QPalette listPalette = m_listWidget->palette();
    listPalette.setColor(QPalette::Base, m_viewport->panelFieldColor());
    listPalette.setColor(QPalette::Text, m_viewport->textColor());
    listPalette.setColor(QPalette::Highlight, m_viewport->panelBorderColor());
    listPalette.setColor(QPalette::HighlightedText, m_viewport->textColor());
    m_listWidget->setPalette(listPalette);
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
    m_pathEdit->setText(m_currentDir + QLatin1Char('/'));

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
        confirmPath(fullPath);
    } else {
        /* Fills the field rather than saving immediately — a stray
         * double-click shouldn't silently overwrite a file. */
        m_pathEdit->setText(fullPath);
        m_pathEdit->setFocus();
    }
}

void FileBrowserPanel::confirmPath(const QString &rawPath) {
    QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QString resolved = QDir::isAbsolutePath(trimmed) ? trimmed : QDir(m_currentDir).filePath(trimmed);

    if (QFileInfo(resolved).isDir()) {
        setDirectory(resolved);
        return;
    }

    if (m_mode == Mode::Open) {
        m_viewport->openFile(resolved);
    } else {
        m_viewport->saveAs(resolved);
    }
    hideBar();
}

/* Return is handled here for *both* widgets, not left to their native
 * Return handling (QLineEdit's returnPressed signal / QListWidget's
 * own Return-triggers-itemActivated) — an event filter runs *before*
 * the target's own handling, so returning true here fully consumes the
 * key press before Qt's native path ever sees it. This isn't
 * stylistic: relying on the native signals had a real, reproducible
 * bug. confirmPath()/activateEntry() can end in hideBar(), which moves
 * focus to the viewport *synchronously*, from inside the very key-press
 * handling that's still in progress. With the native path, Qt's own
 * event dispatch — for both QLineEdit and QListWidget — went on to
 * redeliver that same logical key press to the now-focused
 * EditorViewport afterward, inserting a stray newline into whatever
 * file had just been opened/saved. Consuming the event up front avoids
 * it entirely. QListWidget's itemActivated stays connected for
 * double-click, which never goes through this KeyPress path. */
bool FileBrowserPanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && (watched == m_pathEdit || watched == m_listWidget)) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideBar();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (watched == m_pathEdit) {
                confirmPath(m_pathEdit->text());
            } else {
                QListWidgetItem *item = m_listWidget->currentItem();
                if (item != nullptr) {
                    activateEntry(item->text());
                }
            }
            return true;
        }
    }
    return FloatingPanel::eventFilter(watched, event);
}
