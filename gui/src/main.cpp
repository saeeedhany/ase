#include <QApplication>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPalette>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include "about_panel.h"
#include "buffer_bar.h"
#include "command_line.h"
#include "completion_popup.h"
#include "editor_viewport.h"
#include "lsp_registry.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "help_panel.h"
#include "hover_panel.h"
#include "motion.h"
#include "notification.h"
#include "output_panel.h"

extern "C" {
#include "ase/buffer.h"
}

namespace {

QString windowTitleFor(const QString &filePath, bool dirty) {
    QString name = filePath.isEmpty() ? QStringLiteral("untitled") : filePath;
    return (dirty ? QStringLiteral("%1 [modified] — Absolute Simple Editor") : QStringLiteral("%1 — Absolute Simple Editor"))
        .arg(name);
}

/* Name only; the full path is already in the window title. */
QString bufferLabelFor(const QString &filePath) {
    if (filePath.isEmpty()) {
        return QStringLiteral("untitled");
    }
    return QFileInfo(filePath).fileName();
}

/* Owns the open buffers, one EditorViewport each, stacked so exactly
 * one is visible. See docs/adr/0054, docs/adr/0044. */
class MainWindow : public QMainWindow {
public:
    MainWindow();

    /* Opens `path`, or switches to it if already open. */
    /* The path survives close-and-reopen; the id identifies an untitled
     * buffer, which has neither. See docs/adr/0070. */
    struct JumpEntry {
        QString path;
        quintptr bufferId = 0;
        int line = 1;
        int column = 1;
    };

    void recordJump();
    void jumpBy(int direction);
    bool restoreJump(const JumpEntry &entry);

    void showMessage(NotifyLevel level, const QString &text);
    void showLspState(LspState state, const QString &serverName);
    void updateMessageElision();
    void clearStickyMessage();
    void hideMessage();
    void openBuffer(const QString &path);
    /* Ctrl+N. Saving routes through Save-As, since the path is empty. */
    void newBuffer();
    /* Returns the viewport, so main() can arm the welcome greeting. */
    EditorViewport *addBuffer(AseBuffer *buffer, const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    EditorViewport *activeViewport() const;
    void setActiveIndex(int index);
    void closeBuffer(int index, bool force = false);
    void cycleBuffer(int delta);
    void refreshBufferBar();
    bool confirmDiscard(const QString &message);

    /* Set by :q! so closeEvent does not re-ask what :q! already answered. */
    bool m_forceClose = false;
    /* One per window: buffers in the same project share a server. */
    LspRegistry *m_lspRegistry = nullptr;
    QStackedWidget *m_stack = nullptr;
    BufferBar *m_bufferBar = nullptr;
    OutputPanel *m_outputPanel = nullptr;
    CommandLine *m_commandLine = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    /* Shares the bar with the mode label rather than replacing it. */
    /* Permanent, unlike the message beside it: it answers a question
     * asked at arbitrary moments, not when something happens. */
    QLabel *m_lspLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QGraphicsOpacityEffect *m_messageOpacity = nullptr;
    QPropertyAnimation *m_messageFade = nullptr;
    QTimer *m_messageTimer = nullptr;
    /* Armed one event-loop turn late, so the keystroke that produced
     * the error cannot also dismiss it. */
    /* Pre-elision, since the elided form is recomputed on every resize
     * and cannot be derived from an already-elided string. */
    QString m_messageFullText;
    bool m_messageSticky = false;
    bool m_messageStickyArmed = false;
    QVector<EditorViewport *> m_viewports;
    /* Same shape as the undo stack: a new entry truncates everything
     * after the cursor. index == size() means "at the present". */
    QVector<JumpEntry> m_jumps;
    int m_jumpIndex = 0;
};

/* QStatusBar defaults to a native light bar. Re-applied on every
 * statusChanged, so a hot-reloaded colour reaches it too. */
void applyStatusBarTheme(QMainWindow &window, QLabel *modeLabel, QLabel *statusLabel, EditorViewport *viewport) {
    QPalette pal = window.statusBar()->palette();
    pal.setColor(QPalette::Window, viewport->backgroundColor());
    pal.setColor(QPalette::WindowText, viewport->textColor());
    window.statusBar()->setPalette(pal);
    window.statusBar()->setAutoFillBackground(true);
    modeLabel->setPalette(pal);
    statusLabel->setPalette(pal);
}

/* A working server is named, not announced. Only the failure states
 * take the error colour. "No server configured" stays quiet but
 * present — a blank bar is what read as broken. */
QString lspLabelText(LspState state, const QString &serverName) {
    switch (state) {
    case LspState::NotApplicable:
        return QString();
    case LspState::Unconfigured:
        return QStringLiteral("no lsp");
    case LspState::Starting:
        return QStringLiteral("%1…").arg(serverName);
    case LspState::Running:
        return serverName;
    case LspState::Failed:
        return QStringLiteral("%1 failed").arg(serverName);
    case LspState::Stopped:
        return QStringLiteral("%1 stopped").arg(serverName);
    }
    return QString();
}

/* So the message never butts against the position readout. */
constexpr int kMessageGutter = 32;

QColor lspLabelColor(LspState state, EditorViewport *viewport) {
    if (state == LspState::Failed || state == LspState::Stopped) {
        return viewport->diagnosticErrorColor();
    }
    QColor color = viewport->textColor();
    /* Reference information, not something being said to you. */
    color.setAlpha(state == LspState::Running ? 120 : 150);
    return color;
}

/* Info and warnings share the bar's text colour, separated by how long
 * they linger rather than by hue; info drops one opacity tier. Errors
 * reuse diagnostic_error, so no new colour enters the palette. */
QColor messageColorFor(NotifyLevel level, EditorViewport *viewport) {
    if (level == NotifyLevel::Error) {
        return viewport->diagnosticErrorColor();
    }
    QColor color = viewport->textColor();
    if (level == NotifyLevel::Info) {
        color.setAlpha(210);
    }
    return color;
}

/* Milliseconds before fading. Info is a glance, a warning is a
 * sentence; an error does not time out at all. */
int messageHoldFor(NotifyLevel level) {
    switch (level) {
    case NotifyLevel::Info:
        return 3000;
    case NotifyLevel::Warning:
        return 6000;
    case NotifyLevel::Error:
        return 0;
    }
    return 3000;
}

MainWindow::MainWindow() {
    /* Plain layout rows, not floating chrome. One output panel for the
     * window: the last :compile is a window property, not a file's. */
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    m_bufferBar = new BufferBar(central);
    centralLayout->addWidget(m_bufferBar);

    m_stack = new QStackedWidget(central);
    centralLayout->addWidget(m_stack, 1);

    m_lspRegistry = new LspRegistry(this);
    m_outputPanel = new OutputPanel(nullptr, central);
    centralLayout->addWidget(m_outputPanel);
    /* Opening the file is the window's job, landing on the line the
     * viewport's, so the two are joined here. */
    connect(m_outputPanel, &OutputPanel::hitActivated, this, [this](const QString &path, int line) {
        recordJump();
        openBuffer(path);
        EditorViewport *viewport = activeViewport();
        if (viewport != nullptr) {
            viewport->goToLine(line);
            viewport->setFocus();
        }
    });
    setCentralWidget(central);

    connect(m_bufferBar, &BufferBar::bufferSelected, this, [this](int index) { setActiveIndex(index); });
    connect(m_bufferBar, &BufferBar::bufferCloseRequested, this, [this](int index) { closeBuffer(index); });
    connect(m_bufferBar, &BufferBar::newBufferRequested, this, [this]() { newBuffer(); });

    statusBar()->setSizeGripEnabled(false);
    /* addWidget, so it lands in the left-aligned message area. Empty
     * when Vim mode is off, taking no space. */
    m_modeLabel = new QLabel();
    statusBar()->addWidget(m_modeLabel);
    /* Grows into the free middle of the bar; elided rather than
     * pushing the position readout off the edge. */
    m_messageLabel = new QLabel();
    m_messageLabel->setTextFormat(Qt::PlainText);
    m_messageLabel->setContentsMargins(8, 0, 0, 0);
    m_messageOpacity = new QGraphicsOpacityEffect(m_messageLabel);
    m_messageOpacity->setOpacity(0.0);
    m_messageLabel->setGraphicsEffect(m_messageOpacity);
    m_messageFade = new QPropertyAnimation(m_messageOpacity, "opacity", this);
    motion::apply(m_messageFade, motion::kChrome);
    m_messageTimer = new QTimer(this);
    m_messageTimer->setSingleShot(true);
    connect(m_messageTimer, &QTimer::timeout, this, [this]() { hideMessage(); });
    statusBar()->addWidget(m_messageLabel, 1);
    /* Takes the mode and message area while it is up; the position and
     * LSP readouts to the right stay put. See docs/adr/0073. */
    m_commandLine = new CommandLine();
    statusBar()->addWidget(m_commandLine, 1);
    /* addWidget() shows what it is given, undoing the hide() in the
     * constructor — and a shown-but-transparent line still takes its
     * share of the stretch and its own height, which moved every other
     * label by a pixel. */
    m_commandLine->hide();
    connect(m_commandLine, &CommandLine::promptOpened, this, [this]() {
        hideMessage();
        m_modeLabel->hide();
        m_messageLabel->hide();
    });
    connect(m_commandLine, &CommandLine::promptClosed, this, [this]() {
        m_modeLabel->show();
        m_messageLabel->show();
    });
    /* addPermanentWidget appends right-to-left, so this lands left of
     * the position readout. */
    m_lspLabel = new QLabel();
    m_lspLabel->setTextFormat(Qt::PlainText);
    m_lspLabel->setContentsMargins(0, 0, 12, 0);
    statusBar()->addPermanentWidget(m_lspLabel);
    m_statusLabel = new QLabel(QStringLiteral("Ln 1, Col 1"));
    statusBar()->addPermanentWidget(m_statusLabel);

    /* Window-level, not viewport keys: they act on the buffer list.
     * Qt dispatches these before the focus widget sees them. */
    auto *next = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this);
    connect(next, &QShortcut::activated, this, [this]() { cycleBuffer(1); });
    auto *prev = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this);
    connect(prev, &QShortcut::activated, this, [this]() { cycleBuffer(-1); });
    auto *close = new QShortcut(QKeySequence(QStringLiteral("Ctrl+W")), this);
    connect(close, &QShortcut::activated, this, [this]() { closeBuffer(m_stack->currentIndex()); });
    auto *newFile = new QShortcut(QKeySequence(QStringLiteral("Ctrl+N")), this);
    connect(newFile, &QShortcut::activated, this, [this]() { newBuffer(); });

    /* Window shortcuts: the jumplist spans buffers. */
    auto *jumpBack = new QShortcut(QKeySequence(QStringLiteral("Ctrl+O")), this);
    connect(jumpBack, &QShortcut::activated, this, [this]() { jumpBy(-1); });
    auto *jumpBackAlt = new QShortcut(QKeySequence(QStringLiteral("Alt+Left")), this);
    connect(jumpBackAlt, &QShortcut::activated, this, [this]() { jumpBy(-1); });
    auto *jumpForward = new QShortcut(QKeySequence(QStringLiteral("Ctrl+I")), this);
    connect(jumpForward, &QShortcut::activated, this, [this]() { jumpBy(1); });
    auto *jumpForwardAlt = new QShortcut(QKeySequence(QStringLiteral("Alt+Right")), this);
    connect(jumpForwardAlt, &QShortcut::activated, this, [this]() { jumpBy(1); });
}

EditorViewport *MainWindow::activeViewport() const {
    int index = m_stack->currentIndex();
    return (index >= 0 && index < m_viewports.size()) ? m_viewports[index] : nullptr;
}

EditorViewport *MainWindow::addBuffer(AseBuffer *buffer, const QString &path) {
    auto *viewport = new EditorViewport(buffer, path);

    /* FloatingPanels centre over their own host, so each buffer needs
     * its own set. Inert until shown. See docs/adr/0022. */
    viewport->setFindBar(new FindBar(viewport));
    viewport->setFileBrowser(new FileBrowserPanel(viewport));
    viewport->setHelpPanel(new HelpPanel(viewport));
    /* AboutPanel is built on first Alt+I instead: it decodes and
     * smooth-scales the app icon, which cost 8ms of every buffer's
     * construction for a panel almost nobody opens. See docs/adr/0094. */
    /* One for the window: there is one status bar. */
    viewport->setCommandLine(m_commandLine);
    /* Not FloatingPanels: both track the caret and refresh constantly. */
    viewport->setCompletionPopup(new CompletionPopup(viewport));
    viewport->setHoverPanel(new HoverPanel(viewport));
    viewport->setOutputPanel(m_outputPanel);
    viewport->setLspRegistry(m_lspRegistry);

    connect(viewport, &EditorViewport::statusChanged, this,
            [this, viewport](int line, int column, bool dirty, const QString &mode) {
                if (viewport != activeViewport()) {
                    return; /* a background buffer's cursor is not what the status bar reports */
                }
                clearStickyMessage();
                m_modeLabel->setText(mode);
                m_statusLabel->setText(QStringLiteral("Ln %1, Col %2%3")
                                            .arg(line)
                                            .arg(column)
                                            .arg(dirty ? QStringLiteral(" *") : QString()));
                setWindowTitle(windowTitleFor(viewport->filePath(), dirty));
                applyStatusBarTheme(*this, m_modeLabel, m_statusLabel, viewport);
                showLspState(viewport->lspState(), viewport->lspServerName());
                refreshBufferBar(); /* the name may have changed via Save-As */
            });
    connect(viewport, &EditorViewport::jumpRecorded, this, [this]() { recordJump(); });
    connect(viewport, &EditorViewport::fileOpenRequested, this, [this](const QString &path) {
        /* Opening a file is a jump. */
        recordJump();
        openBuffer(path);
    });
    /* Same split as a search hit, reached from the viewport. */
    connect(viewport, &EditorViewport::fileOpenAtLineRequested, this,
            [this](const QString &path, int line) {
                openBuffer(path);
                EditorViewport *opened = activeViewport();
                if (opened != nullptr) {
                    opened->goToLine(line);
                    opened->setFocus();
                }
            });
    /* Only the buffer you are looking at gets to speak — same rule the
     * status readout follows, and for the same reason: a message about a
     * file in another tab, with nothing naming that file, reads as a
     * message about this one. */
    connect(viewport, &EditorViewport::closeRequested, this, [this, viewport](bool force) {
        closeBuffer(m_viewports.indexOf(viewport), force);
    });
    connect(viewport, &EditorViewport::lspStateChanged, this,
            [this, viewport](LspState state, const QString &serverName) {
                if (viewport == activeViewport()) {
                    showLspState(state, serverName);
                }
            });
    connect(viewport, &EditorViewport::messagePosted, this,
            [this, viewport](NotifyLevel level, const QString &text) {
                if (viewport == activeViewport()) {
                    showMessage(level, text);
                }
            });

    m_viewports.push_back(viewport);
    m_stack->addWidget(viewport);
    setActiveIndex(m_viewports.size() - 1);
    return viewport;
}

/*
 * Renders one message in the status bar: fade in on the app's own motion
 * tier, hold for a level-dependent beat, fade out. A new message
 * replaces whatever was there rather than queueing — a queue means the
 * thing you are being told about happened several seconds ago, which is
 * worse than missing it. See docs/adr/0062.
 */
/* Vim's is 100; no reason to disagree with a number that has been lived
 * with for thirty years. */
constexpr int kMaxJumps = 100;

/* Snapshots where the cursor is, as the place to come back to. Called
 * before a jump, never after. */
void MainWindow::recordJump() {
    EditorViewport *viewport = activeViewport();
    if (viewport == nullptr) {
        return;
    }

    JumpEntry entry;
    entry.path = viewport->filePath();
    entry.bufferId = reinterpret_cast<quintptr>(viewport);
    entry.line = viewport->cursorLine();
    entry.column = viewport->cursorColumn();

    /* Without this, `gd` twice on one symbol records two identical
     * entries and the first Ctrl+O appears to do nothing. */
    if (!m_jumps.isEmpty() && m_jumpIndex > 0) {
        const JumpEntry &previous = m_jumps[m_jumpIndex - 1];
        if (previous.bufferId == entry.bufferId && previous.line == entry.line) {
            return;
        }
    }

    /* A new jump abandons the forward history, as an edit does redo. */
    m_jumps.resize(m_jumpIndex);
    m_jumps.push_back(entry);
    if (m_jumps.size() > kMaxJumps) {
        m_jumps.removeFirst();
    }
    m_jumpIndex = m_jumps.size();
}

/* `direction` is -1 for back, +1 for forward. Going back from the
 * present records it first, or back is a one-way door. */
void MainWindow::jumpBy(int direction) {
    EditorViewport *viewport = activeViewport();
    if (viewport == nullptr) {
        return;
    }

    if (direction < 0) {
        if (m_jumpIndex <= 0) {
            viewport->notify(NotifyLevel::Warning, QStringLiteral("no earlier position"));
            return;
        }
        if (m_jumpIndex == m_jumps.size()) {
            JumpEntry here;
            here.path = viewport->filePath();
            here.bufferId = reinterpret_cast<quintptr>(viewport);
            here.line = viewport->cursorLine();
            here.column = viewport->cursorColumn();
            m_jumps.push_back(here);
        }
        m_jumpIndex--;
    } else {
        if (m_jumpIndex + 1 >= m_jumps.size()) {
            viewport->notify(NotifyLevel::Warning, QStringLiteral("no later position"));
            return;
        }
        m_jumpIndex++;
    }

    if (!restoreJump(m_jumps[m_jumpIndex])) {
        /* Gone and unreopenable; drop it rather than leave a dead step. */
        m_jumps.remove(m_jumpIndex);
        m_jumpIndex = std::clamp(m_jumpIndex, 0, static_cast<int>(m_jumps.size()));
        viewport->notify(NotifyLevel::Warning, QStringLiteral("that buffer is gone"));
    }
}

bool MainWindow::restoreJump(const JumpEntry &entry) {
    for (int i = 0; i < m_viewports.size(); ++i) {
        if (reinterpret_cast<quintptr>(m_viewports[i]) == entry.bufferId) {
            setActiveIndex(i);
            m_viewports[i]->goToLineColumn(entry.line, entry.column);
            m_viewports[i]->setFocus();
            return true;
        }
    }
    if (entry.path.isEmpty()) {
        return false; /* an untitled buffer, closed: nothing to reopen */
    }
    /* Closed since, but it has a path — reopen it, as vim does. */
    openBuffer(entry.path);
    EditorViewport *opened = activeViewport();
    if (opened == nullptr) {
        return false;
    }
    opened->goToLineColumn(entry.line, entry.column);
    opened->setFocus();
    return true;
}

void MainWindow::showMessage(NotifyLevel level, const QString &text) {
    EditorViewport *viewport = activeViewport();
    if (viewport == nullptr) {
        return;
    }

    QPalette pal = m_messageLabel->palette();
    pal.setColor(QPalette::WindowText, messageColorFor(level, viewport));
    m_messageLabel->setPalette(pal);
    m_messageFullText = text;
    m_messageLabel->setToolTip(text);
    updateMessageElision();

    m_messageFade->stop();
    m_messageFade->setStartValue(m_messageOpacity->opacity());
    m_messageFade->setEndValue(1.0);
    m_messageFade->start();

    int hold = messageHoldFor(level);
    m_messageSticky = (hold == 0);
    m_messageStickyArmed = false;
    if (hold > 0) {
        m_messageTimer->start(hold + motion::kChrome);
    } else {
        m_messageTimer->stop();
        /* Next turn: the keystroke that caused this is still being
         * delivered and would dismiss it on its way out. */
        QTimer::singleShot(0, this, [this]() { m_messageStickyArmed = true; });
    }
}

void MainWindow::hideMessage() {
    m_messageFullText.clear();
    m_messageSticky = false;
    m_messageStickyArmed = false;
    m_messageTimer->stop();
    m_messageFade->stop();
    m_messageFade->setStartValue(m_messageOpacity->opacity());
    m_messageFade->setEndValue(0.0);
    m_messageFade->start();
}

/* An error sits there until you do something else — any cursor move or
 * edit, which is exactly what statusChanged already fires on. Timing
 * errors out was rejected: a failed save missed while looking elsewhere
 * is how people lose work. */
void MainWindow::clearStickyMessage() {
    if (m_messageSticky && m_messageStickyArmed) {
        hideMessage();
    }
}

/* Elides rather than squeezing Ln/Col off the bar. Recomputed on every
 * resize: the first message of a session is posted before show(), when
 * the bar's width is still a meaningless default. */
void MainWindow::updateMessageElision() {
    if (m_messageFullText.isEmpty()) {
        m_messageLabel->clear();
        return;
    }
    QFontMetrics metrics(m_messageLabel->font());
    int taken = m_modeLabel->sizeHint().width() + m_lspLabel->sizeHint().width() +
                m_statusLabel->sizeHint().width();
    int room = std::max(120, statusBar()->width() - taken - kMessageGutter);
    m_messageLabel->setText(metrics.elidedText(m_messageFullText, Qt::ElideMiddle, room));
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateMessageElision();
}

void MainWindow::showLspState(LspState state, const QString &serverName) {
    EditorViewport *viewport = activeViewport();
    if (viewport == nullptr) {
        return;
    }
    QString text = lspLabelText(state, serverName);
    QColor color = lspLabelColor(state, viewport);
    /* Called on every statusChanged so a hot-reloaded theme reaches
     * this label; the guard keeps it off the keystroke path. */
    if (text == m_lspLabel->text() && color == m_lspLabel->palette().color(QPalette::WindowText)) {
        return;
    }
    QPalette pal = m_lspLabel->palette();
    pal.setColor(QPalette::WindowText, color);
    m_lspLabel->setPalette(pal);
    m_lspLabel->setText(text);
}

void MainWindow::openBuffer(const QString &path) {
    for (int i = 0; i < m_viewports.size(); ++i) {
        if (m_viewports[i]->filePath() == path) {
            setActiveIndex(i);
            return;
        }
    }

    /* Missing files start empty, keeping `path` as the save target. */
    AseBuffer *buffer = ase_buffer_create_from_file(path.toUtf8().constData());
    if (buffer == nullptr) {
        buffer = ase_buffer_create();
    }
    if (buffer == nullptr) {
        return;
    }
    addBuffer(buffer, path);
}

void MainWindow::newBuffer() {
    AseBuffer *buffer = ase_buffer_create();
    if (buffer != nullptr) {
        addBuffer(buffer, QString());
    }
}

void MainWindow::setActiveIndex(int index) {
    if (index < 0 || index >= m_viewports.size()) {
        return;
    }
    m_stack->setCurrentIndex(index);
    /* A message from the buffer you just left would read as being
     * about this one. */
    hideMessage();
    EditorViewport *viewport = m_viewports[index];
    /* Each buffer has its own server, so this follows the visible file. */
    showLspState(viewport->lspState(), viewport->lspServerName());
    m_outputPanel->setViewport(viewport);
    m_commandLine->setViewport(viewport);
    viewport->onActivated(); /* focuses, and starts its LSP the first time */
    refreshBufferBar();
    /* Through the same statusChanged path every edit uses, so there is
     * no second copy of the formatting. */
    viewport->emitInitialStatus();
}

void MainWindow::closeBuffer(int index, bool force) {
    if (index < 0 || index >= m_viewports.size()) {
        return;
    }

    EditorViewport *viewport = m_viewports[index];
    if (!force && viewport->isDirty() &&
        !confirmDiscard(QStringLiteral("\"%1\" has unsaved changes. Close it anyway?")
                             .arg(bufferLabelFor(viewport->filePath())))) {
        return;
    }

    /* The last buffer closing is the window closing. Asked after the
     * dirty check, so :q! skips the prompt here too rather than meeting
     * closeEvent's. */
    if (m_viewports.size() == 1) {
        if (force) {
            m_forceClose = true;
        }
        close();
        return;
    }

    m_viewports.remove(index);
    m_stack->removeWidget(viewport);
    viewport->deleteLater();
    setActiveIndex(std::min(index, static_cast<int>(m_viewports.size()) - 1));
}

void MainWindow::cycleBuffer(int delta) {
    if (m_viewports.size() < 2) {
        return;
    }
    int count = m_viewports.size();
    setActiveIndex(((m_stack->currentIndex() + delta) % count + count) % count);
}

void MainWindow::refreshBufferBar() {
    QVector<BufferBar::Item> items;
    items.reserve(m_viewports.size());
    for (EditorViewport *viewport : m_viewports) {
        /* A stable unique handle; display names collide (every new
         * buffer is "untitled"). */
        items.push_back({reinterpret_cast<quintptr>(viewport), bufferLabelFor(viewport->filePath()),
                          viewport->isDirty()});
    }
    EditorViewport *viewport = activeViewport();
    if (viewport != nullptr) {
        m_bufferBar->setColors(viewport->backgroundColor(), viewport->textColor());
        m_bufferBar->setBaseFont(viewport->editorFont());
    }
    m_bufferBar->setEntries(items, m_stack->currentIndex());
}

/* Built manually rather than QMessageBox::question(), so a stylesheet
 * can be applied before showing — the native palette and coloured icon
 * were a reported complaint. See docs/adr/0044. */
bool MainWindow::confirmDiscard(const QString &message) {
    EditorViewport *themeSource = activeViewport();
    if (themeSource == nullptr) {
        return true;
    }
    QColor bg = themeSource->panelBackgroundColor();
    QColor border = themeSource->panelBorderColor();
    QColor text = themeSource->textColor();

    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setWindowTitle(QStringLiteral("Unsaved changes"));
    box.setText(message);
    QPushButton *discardButton = box.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancelButton);
    box.setStyleSheet(QStringLiteral("QMessageBox { background-color: %1; }"
                                      "QMessageBox QLabel { color: %2; }"
                                      "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                                      "padding: 4px 14px; min-width: 60px; }"
                                      "QPushButton:hover, QPushButton:default { border-color: %2; }")
                           .arg(bg.name(), text.name(), border.name()));
    box.exec();
    return box.clickedButton() == discardButton;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_forceClose) {
        event->accept();
        return;
    }

    QVector<EditorViewport *> dirty;
    for (EditorViewport *viewport : m_viewports) {
        if (viewport->isDirty()) {
            dirty.push_back(viewport);
        }
    }
    if (dirty.isEmpty()) {
        event->accept();
        return;
    }

    QString message;
    if (dirty.size() == 1) {
        message = QStringLiteral("\"%1\" has unsaved changes. Quit without saving?")
                      .arg(bufferLabelFor(dirty.first()->filePath()));
    } else {
        QStringList names;
        for (EditorViewport *viewport : dirty) {
            names << bufferLabelFor(viewport->filePath());
        }
        message = QStringLiteral("%1 files have unsaved changes (%2). Quit without saving?")
                      .arg(dirty.size())
                      .arg(names.join(QStringLiteral(", ")));
    }

    if (confirmDiscard(message)) {
        event->accept();
    } else {
        event->ignore();
    }
}
} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    /* Bundled via Qt resources; reused in the About panel. */
    app.setWindowIcon(QIcon(QStringLiteral(":/ase.png")));

    /* The panel fields draw their own caret now, but anything native
     * appearing later still blinks on the editor's cadence rather than
     * the platform's ~1000ms default. */
    app.setCursorFlashTime(720);

    QString filePath;
    AseBuffer *buffer;

    if (argc > 1) {
        filePath = QString::fromLocal8Bit(argv[1]);
        buffer = ase_buffer_create_from_file(argv[1]);
        if (buffer == nullptr) {
            /* v1 doesn't distinguish "new file" from "read error". */
            buffer = ase_buffer_create();
        }
    } else {
        buffer = ase_buffer_create();
    }

    if (buffer == nullptr) {
        return 1;
    }

    MainWindow window;
    window.resize(900, 650);
    /* Launching with no file is the only situation the welcome
     * greeting belongs to, and only here is that knowable: a Ctrl+N
     * buffer looks identical to the viewport. See docs/adr/0058. */
    EditorViewport *first = window.addBuffer(buffer, filePath);
    if (filePath.isEmpty()) {
        first->armWelcomeGreeting();
    }
    window.show();

    return app.exec();
}
