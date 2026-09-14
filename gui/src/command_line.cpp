#include "command_line.h"

#include "editor_viewport.h"
#include "motion.h"
#include "smooth_line_edit.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPropertyAnimation>

CommandLine::CommandLine(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->setSpacing(0); /* `:42`, not `: 42` */

    /* The prompt character is the label — vim's own affordance, and the
     * reason this needs no LetterBadge the way the floating panels do. */
    m_prefix = new QLabel(QStringLiteral(":"), this);
    m_prefix->setTextFormat(Qt::PlainText);
    layout->addWidget(m_prefix);

    m_edit = new SmoothLineEdit(this);
    m_edit->setFrame(false);
    m_edit->setTextMargins(0, 0, 0, 0);
    /* A line edit's size hint is a couple of pixels taller than a label's,
     * and QStatusBar sizes itself from every item it holds including the
     * hidden ones — so without this the bar grows by 2px the moment this
     * is added, moving every other label in it whether or not the prompt
     * is ever opened. */
    m_edit->setFixedHeight(m_prefix->sizeHint().height());
    layout->addWidget(m_edit, 1);

    /* A fade, not the panels' scale+fade: the bar is a fixed strip, and
     * scaling a line of it reads as the chrome itself moving. */
    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(0.0);
    setGraphicsEffect(m_opacity);
    m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
    motion::apply(m_fade, motion::kChrome);

    m_edit->installEventFilter(this);
    hide();
}

void CommandLine::setViewport(EditorViewport *viewport) {
    if (m_viewport == viewport) {
        return;
    }
    m_viewport = viewport;
    refreshTheme();
}

void CommandLine::openPrompt(QChar prefix) {
    if (m_viewport == nullptr) {
        return;
    }
    m_prefix->setText(QString(prefix));
    refreshTheme();
    m_edit->clear();
    m_open = true;
    emit promptOpened();
    show();
    m_fade->stop();
    if (m_viewport->animationsEnabled()) {
        m_fade->setStartValue(m_opacity->opacity());
        m_fade->setEndValue(1.0);
        m_fade->start();
    } else {
        m_opacity->setOpacity(1.0);
    }
    m_edit->setFocus();
}

void CommandLine::closePrompt() {
    if (!m_open) {
        return;
    }
    m_open = false;
    m_fade->stop();
    m_opacity->setOpacity(0.0);
    hide();
    emit promptClosed();
    if (m_viewport != nullptr) {
        m_viewport->setFocus();
    }
}

void CommandLine::refreshTheme() {
    if (m_viewport == nullptr) {
        return;
    }
    QPalette pal = m_prefix->palette();
    QColor text = m_viewport->textColor();
    pal.setColor(QPalette::WindowText, text);
    m_prefix->setPalette(pal);
    m_edit->applyPanelTheme(m_viewport);
    /* applyPanelTheme gives the field a panel's raised tone, which is
     * right inside a floating panel and wrong here: on the status bar it
     * has to read as a line of text, not a control sitting in it. */
    QPalette editPal = m_edit->palette();
    editPal.setColor(QPalette::Base, Qt::transparent);
    m_edit->setPalette(editPal);
}

/* Return/Escape are consumed here rather than through QLineEdit's own
 * returnPressed: closing moves focus to the viewport synchronously,
 * mid-key-press, and Qt's native path would redeliver the same press to
 * the newly focused editor. Same reason FileBrowserPanel does this. */
bool CommandLine::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && watched == m_edit) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            closePrompt();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            run();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CommandLine::run() {
    QString command = m_edit->text();
    EditorViewport *viewport = m_viewport;
    closePrompt();
    if (viewport != nullptr) {
        viewport->runCommand(command);
    }
}
