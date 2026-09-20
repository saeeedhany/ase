#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QDir>
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
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include "about_panel.h"
#include "buffer_bar.h"
#include "command_line.h"
#include "completion_popup.h"
#include "editor_viewport.h"
#include "ase/keymap.h"
#include "keybindings.h"
#include "command_registry.h"
#include "ase/recovery.h"
#include "ase/session.h"
#include "ase/theme.h"
#include "themed_dialog.h"
#include "lsp_registry.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "help_panel.h"
#include "hover_panel.h"
#include "motion.h"
#include "notification.h"
#include "output_panel.h"
#include "project_edit.h"

extern "C" {
#include "ase/buffer.h"
}

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#endif

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
    /* exact=false lands on the line's first non-blank, which is what
     * `'A` means against `` `A ``. */
    bool restoreJump(const JumpEntry &entry, bool exact = true);
    void setGlobalMark(EditorViewport *owner, char name);
    void jumpToGlobalMark(EditorViewport *asker, char name, bool exact);

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
    bool eventFilter(QObject *watched, QEvent *event) override;
    QString m_pendingPrefix;
    void resizeEvent(QResizeEvent *event) override;

private:
    EditorViewport *activeViewport() const;
    void setActiveIndex(int index);
    void closeBuffer(int index, bool force = false);
    void cycleBuffer(int delta);
    void refreshBufferBar();
    bool confirmDiscard(const QString &message);
    void askAboutRecovery(EditorViewport *viewport, const QString &path);
    void saveSession();
    static constexpr int kPanelResizeStep = 60;
    void focusRegion(int direction);
    void closeFocusedRegion();
    void discardEveryRecovery();
    void applyProjectReplace(const QString &root,
                              const QVector<project::Replacement> &replacements, int needleLength,
                              const QByteArray &replacement);
    EditorViewport *viewportForPath(const QString &path);
    void registerWindowCommands();
    void installWindowShortcuts();
    CommandRegistry m_commands;
    bool m_restoringSession = false;
    QString m_lastSessionSignature;

public:
    bool restoreSession();
    /* Offers back untitled buffers a crash took, and drops snapshots
     * old enough that nobody is coming for them. See docs/adr/0127. */
    void recoverUntitledBuffers();
    /* Writes the Reference pages from the live tables. See
     * docs/adr/0126. */
    bool dumpDocs(const QString &outDir);

private:

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
    QLabel *m_pendingLabel = nullptr;
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
    /* A-Z, shared by every buffer: a global mark names a file as well
     * as a position, which is the jumplist's shape exactly. */
    QHash<char, JumpEntry> m_globalMarks;
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
    connect(m_outputPanel, &OutputPanel::replaceRequested, this,
            [this](const QString &root, const QVector<project::Replacement> &replacements,
                   int needleLength, const QByteArray &replacement) {
                applyProjectReplace(root, replacements, needleLength, replacement);
            });
    connect(m_outputPanel, &OutputPanel::hitActivated, this, [this](const QString &path, int line) {
        recordJump();
        openBuffer(path);
        EditorViewport *viewport = activeViewport();
        if (viewport != nullptr) {
            viewport->goToLine(line);
        }
        /* Focus stays in the list. Walking a list of references is a
         * sequence, not one jump — taking the keyboard away after each
         * one meant reaching back for it every time. Ctrl+W k goes to
         * the editor when you have arrived somewhere worth staying.
         * openBuffer() may have focused a viewport on the way, so this
         * takes it back rather than merely not giving it away. */
        m_outputPanel->focusList();
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
    /* Vim's showcmd, immediately left of the position readout — where
     * vim itself puts it, and where the eye already goes for "what is
     * the editor doing". */
    m_pendingLabel = new QLabel();
    m_pendingLabel->setTextFormat(Qt::PlainText);
    m_pendingLabel->setContentsMargins(0, 0, 12, 0);
    statusBar()->addPermanentWidget(m_pendingLabel);

    m_lspLabel = new QLabel();
    m_lspLabel->setTextFormat(Qt::PlainText);
    m_lspLabel->setContentsMargins(0, 0, 12, 0);
    statusBar()->addPermanentWidget(m_lspLabel);
    m_statusLabel = new QLabel(QStringLiteral("Ln 1, Col 1"));
    statusBar()->addPermanentWidget(m_statusLabel);

    registerWindowCommands();
    /* On the application, not on this window: the key has to be caught
     * wherever focus happens to be. */
    qApp->installEventFilter(this);

    /* Buffer changes write the session immediately; this catches the
     * caret moving, which is far too hot to write on. Nothing is written
     * unless something actually moved. A SIGTERM or a crash then loses
     * at most a few seconds of scroll position. */
    auto *sessionTimer = new QTimer(this);
    connect(sessionTimer, &QTimer::timeout, this, [this]() { saveSession(); });
    sessionTimer->start(5000);
    installWindowShortcuts();
}

/* Window-level actions: they act on the buffer list or the jumplist,
 * which span buffers and so belong to the window rather than to any one
 * viewport. Registered into the same table the viewport uses, so a
 * config binding reaches them the same way. See docs/adr/0113. */
void MainWindow::registerWindowCommands() {
    m_commands.add(QStringLiteral("buffer.new"), QStringLiteral("New buffer"),
                    [this]() { newBuffer(); });
    m_commands.add(QStringLiteral("buffer.close"), QStringLiteral("Close this buffer"),
                    [this]() { closeBuffer(m_stack->currentIndex()); });
    m_commands.add(QStringLiteral("buffer.next"), QStringLiteral("Next buffer"),
                    [this]() { cycleBuffer(1); });
    m_commands.add(QStringLiteral("buffer.previous"), QStringLiteral("Previous buffer"),
                    [this]() { cycleBuffer(-1); });
    /* Window-level, and deliberately so: resizing the panel is most
     * wanted while reading it, and the panel has focus then — a
     * viewport binding would never see the key. See docs/adr/0117. */
    m_commands.add(QStringLiteral("editor.output-panel.taller"),
                    QStringLiteral("Give the output panel more room"),
                    [this]() { m_outputPanel->growBy(kPanelResizeStep); });
    m_commands.add(QStringLiteral("editor.output-panel.shorter"),
                    QStringLiteral("Give the editor more room"),
                    [this]() { m_outputPanel->growBy(-kPanelResizeStep); });
    /* Regions: the editor and the docked panel. `Ctrl+W` and a vim
     * window key moves between them, closes one, or resizes it — the
     * same vocabulary vim uses for windows, because that is the one
     * already in people's fingers. See docs/adr/0120. */
    /* The window's, not a viewport's, so it opens from wherever the
     * keyboard happens to be. `:q` to close a panel you are reading is
     * the obvious thing to type, and it was unreachable without first
     * going back to the buffer. See docs/adr/0122. */
    m_commands.add(QStringLiteral("editor.command-line"), QStringLiteral("Open the command line"),
                    [this]() {
                        if (m_commandLine != nullptr) {
                            m_commandLine->openPrompt(QLatin1Char(':'));
                        }
                    });
    m_commands.add(QStringLiteral("pane.focus-down"), QStringLiteral("Focus the region below"),
                    [this]() { focusRegion(1); });
    m_commands.add(QStringLiteral("pane.focus-up"), QStringLiteral("Focus the region above"),
                    [this]() { focusRegion(-1); });
    m_commands.add(QStringLiteral("pane.cycle"), QStringLiteral("Cycle between regions"),
                    [this]() { focusRegion(0); });
    m_commands.add(QStringLiteral("pane.close"), QStringLiteral("Close the focused region"),
                    [this]() { closeFocusedRegion(); });
    m_commands.add(QStringLiteral("pane.only"), QStringLiteral("Close everything but the editor"),
                    [this]() {
                        if (m_outputPanel->isVisible()) {
                            m_outputPanel->hide();
                        }
                        if (EditorViewport *v = activeViewport()) {
                            v->setFocus();
                        }
                    });
    m_commands.add(QStringLiteral("editor.jump-back"), QStringLiteral("Back to the previous jump"),
                    [this]() { jumpBy(-1); });
    m_commands.add(QStringLiteral("editor.jump-forward"), QStringLiteral("Forward again"),
                    [this]() { jumpBy(1); });
}

/* Qt dispatches a QShortcut before the focus widget sees the key, which
 * is what these need: closing a buffer has to work while a panel holds
 * focus. They are built from the binding table rather than written out,
 * so rebinding one in config.ase moves it here too. */
void MainWindow::installWindowShortcuts() {
    static const char *const kWindowCommands[] = {
        "buffer.new",         "buffer.close",   "buffer.next",
        "buffer.previous",    "editor.jump-back", "editor.jump-forward",
        "editor.output-panel.taller", "editor.output-panel.shorter",
        "editor.command-line"};
    AseConfig *config = nullptr;
    char *configPath = ase_config_default_path();
    if (configPath != nullptr) {
        config = ase_config_load(configPath);
        free(configPath);
    }

    for (const char *name : kWindowCommands) {
        const QString command = QString::fromLatin1(name);
        for (const QString &chord : keys::chordsFor(config, command)) {
            QKeySequence sequence = keys::sequenceFor(chord);
            if (sequence.isEmpty()) {
                continue;
            }
            auto *shortcut = new QShortcut(sequence, this);
            connect(shortcut, &QShortcut::activated, this,
                    [this, command]() { m_commands.run(command); });
        }
    }
    ase_config_destroy(config);
}

/* The panel is below the editor, so "down" means into it and "up" means
 * out of it. With one region open both fall back to the editor rather
 * than doing nothing, which is what someone pressing them wants. */
void MainWindow::focusRegion(int direction) {
    EditorViewport *viewport = activeViewport();
    bool panelUsable = m_outputPanel != nullptr && m_outputPanel->isVisible();
    bool inPanel = panelUsable && m_outputPanel->hasFocusInside();

    bool wantPanel = false;
    if (direction > 0) {
        wantPanel = panelUsable;
    } else if (direction < 0) {
        wantPanel = false;
    } else {
        wantPanel = panelUsable && !inPanel; /* cycle */
    }

    if (wantPanel) {
        m_outputPanel->focusList();
    } else if (viewport != nullptr) {
        viewport->setFocus();
    }
}

/* vim's `Ctrl+W c`: closes whatever has focus. In the panel that is the
 * panel; in the editor it is the buffer — which is where the old bare
 * `Ctrl+W` went, so nothing was lost by making it a prefix. */
void MainWindow::closeFocusedRegion() {
    if (m_outputPanel != nullptr && m_outputPanel->isVisible() &&
        m_outputPanel->hasFocusInside()) {
        m_outputPanel->hide();
        if (EditorViewport *viewport = activeViewport()) {
            viewport->setFocus();
        }
        return;
    }
    closeBuffer(m_stack->currentIndex());
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
                m_pendingLabel->setPalette(m_statusLabel->palette());
                showLspState(viewport->lspState(), viewport->lspServerName());
                refreshBufferBar(); /* the name may have changed via Save-As */
            });
    connect(viewport, &EditorViewport::pendingInputChanged, this, [this, viewport](const QString &keys) {
        /* Only the buffer in front; a background one cannot be being
         * typed into. */
        if (viewport == m_stack->currentWidget()) {
            m_pendingLabel->setText(keys);
        }
    });
    connect(viewport, &EditorViewport::focusChanged, this, [this](bool focused) {
        if (m_outputPanel != nullptr) {
            m_outputPanel->setRegionActive(!focused && m_outputPanel->hasFocusInside());
        }
    });
    connect(viewport, &EditorViewport::jumpRecorded, this, [this]() { recordJump(); });
    connect(viewport, &EditorViewport::globalMarkSetRequested, this,
            [this, viewport](char name) { setGlobalMark(viewport, name); });
    connect(viewport, &EditorViewport::globalMarkJumpRequested, this,
            [this, viewport](char name, bool exact) { jumpToGlobalMark(viewport, name, exact); });
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
    saveSession();
    m_stack->addWidget(viewport);
    setActiveIndex(m_viewports.size() - 1);
    /* After the signal connections above, not with the other setters:
     * registering checks the config's bindings and reports the bad ones,
     * and a message emitted before anything is listening is lost. */
    viewport->registerCommands(&m_commands);

    /* Deferred rather than asked here: addBuffer runs before the window
     * is shown, and the dialog would open over an unpainted black
     * rectangle. Queued, it arrives once there is an editor behind it. */
    if (viewport->hasRecoverySnapshot()) {
        QPointer<EditorViewport> pending = viewport;
        QTimer::singleShot(0, this, [this, pending, path]() {
            if (pending.isNull() || !pending->hasRecoverySnapshot()) {
                return;
            }
            askAboutRecovery(pending, path);
        });
    }

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

bool MainWindow::restoreJump(const JumpEntry &entry, bool exact) {
    for (int i = 0; i < m_viewports.size(); ++i) {
        if (reinterpret_cast<quintptr>(m_viewports[i]) == entry.bufferId) {
            setActiveIndex(i);
            if (exact) {
                m_viewports[i]->goToLineColumn(entry.line, entry.column);
            } else {
                m_viewports[i]->goToLine(entry.line);
            }
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
    if (exact) {
        opened->goToLineColumn(entry.line, entry.column);
    } else {
        opened->goToLine(entry.line);
    }
    opened->setFocus();
    return true;
}

void MainWindow::setGlobalMark(EditorViewport *owner, char name) {
    JumpEntry entry;
    entry.path = owner->filePath();
    entry.bufferId = reinterpret_cast<quintptr>(owner);
    entry.line = owner->cursorLine();
    entry.column = owner->cursorColumn();
    m_globalMarks.insert(name, entry);

    /* One letter, one place. Without this a buffer that used to hold the
     * mark would still answer `d'A` from its stale local copy. */
    for (EditorViewport *viewport : m_viewports) {
        if (viewport != owner) {
            viewport->clearLocalMark(name);
        }
    }
}

void MainWindow::jumpToGlobalMark(EditorViewport *asker, char name, bool exact) {
    auto it = m_globalMarks.constFind(name);
    if (it == m_globalMarks.constEnd()) {
        asker->notify(NotifyLevel::Warning, QStringLiteral("mark %1 not set").arg(QChar(name)));
        return;
    }
    /* Recorded before moving, so Ctrl+O comes back across the file
     * change too. */
    recordJump();
    if (!restoreJump(*it, exact)) {
        asker->notify(NotifyLevel::Warning, QStringLiteral("mark %1 is gone").arg(QChar(name)));
    }
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

/* A snapshot only outlives a crash, so finding one means the last
 * session ended badly. Asked rather than restored silently: the file may
 * have been changed by something else since, and only the user knows
 * which version they want. Recovering is the default because it is the
 * reversible answer — the restore is an undoable edit, and discarding is
 * not. See docs/adr/0110. */
namespace {

/* An untitled snapshot names the process that wrote it. One still
 * running owns its buffer, so offering that text back would show a
 * second copy of something already on screen in another window. */
bool processIsRunning(qint64 pid) {
#if defined(Q_OS_WIN)
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (handle == nullptr) {
        return false;
    }
    DWORD code = 0;
    bool alive = GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

/* "untitled:<pid>:<serial>" — see EditorViewport::recoveryKey(). */
bool isAbandonedUntitledKey(const QString &key) {
    if (!key.startsWith(QLatin1String("untitled:"))) {
        return false;
    }
    const QStringList parts = key.split(QLatin1Char(':'));
    if (parts.size() != 3) {
        return false;
    }
    bool ok = false;
    qint64 pid = parts[1].toLongLong(&ok);
    if (!ok) {
        return false;
    }
    return pid == QCoreApplication::applicationPid() || !processIsRunning(pid);
}

/* Long enough that a machine left off over a holiday still has its
 * work; short enough that the directory does not grow for ever. */
constexpr long kSnapshotMaxAgeSeconds = 30L * 24 * 60 * 60;

} // namespace

void MainWindow::recoverUntitledBuffers() {
    EditorViewport *any = activeViewport();
    if (any == nullptr || any->recoveryDir().isEmpty()) {
        return;
    }
    const QByteArray dir = any->recoveryDir().toUtf8();

    ase_recovery_prune(dir.constData(), kSnapshotMaxAgeSeconds);

    AseRecoveryList list;
    if (!ase_recovery_list(dir.constData(), &list)) {
        return;
    }
    QStringList orphans;
    for (size_t i = 0; i < list.count; i++) {
        QString key = QString::fromUtf8(list.keys[i]);
        if (isAbandonedUntitledKey(key)) {
            orphans << key;
        }
    }
    ase_recovery_list_free(&list);

    if (orphans.isEmpty()) {
        return;
    }
    orphans.sort();

    const bool one = orphans.size() == 1;
    bool discard = confirmDestructive(
        this, any, QStringLiteral("Unsaved work recovered"),
        one ? QStringLiteral("An unsaved buffer with no filename survived a session that ended "
                              "unexpectedly.")
            : QStringLiteral("%1 unsaved buffers with no filename survived a session that ended "
                              "unexpectedly.")
                  .arg(orphans.size()),
        one ? QStringLiteral("Discard it") : QStringLiteral("Discard them"),
        QStringLiteral("Recover"));

    /* A bare launch leaves one empty untitled buffer. Recovering into a
     * new tab beside it leaves the empty one as clutter nobody asked
     * for, so it goes — but only while it is genuinely untouched. */
    EditorViewport *pristine = nullptr;
    if (!discard && m_viewports.size() == 1 && m_viewports.first()->isUntitled() &&
        !m_viewports.first()->isDirty()) {
        pristine = m_viewports.first();
    }

    for (const QString &key : orphans) {
        if (discard) {
            ase_recovery_remove(dir.constData(), key.toUtf8().constData());
            continue;
        }
        size_t len = 0;
        char *content = ase_recovery_read(dir.constData(), key.toUtf8().constData(), &len);
        if (content == nullptr) {
            continue;
        }
        AseBuffer *buffer = ase_buffer_create();
        if (buffer != nullptr) {
            ase_buffer_insert(buffer, 0, content, len);
            EditorViewport *viewport = addBuffer(buffer, QString());
            if (viewport != nullptr) {
                /* Keeps writing to the snapshot it came from, rather
                 * than opening a second one beside it. */
                viewport->adoptRecoveryKey(key);
                viewport->markUnsaved();
            }
        }
        free(content);
    }

    if (pristine != nullptr && m_viewports.size() > 1) {
        closeBuffer(m_viewports.indexOf(pristine), true);
    }
    /* markUnsaved() lands after addBuffer() built the row, so the dot
     * needs asking for again. */
    refreshBufferBar();
}

/* Defined further down, beside restoreSession() which also needs it. */
static QString sameFileKey(const QString &path);

/* Find-or-open, without making it the buffer you are looking at: a
 * replace touching nine files should not walk you through nine tabs. */
EditorViewport *MainWindow::viewportForPath(const QString &path) {
    const QString wanted = sameFileKey(path);
    for (EditorViewport *viewport : m_viewports) {
        if (!wanted.isEmpty() && sameFileKey(viewport->filePath()) == wanted) {
            return viewport;
        }
    }
    AseBuffer *buffer = ase_buffer_create_from_file(path.toUtf8().constData());
    if (buffer == nullptr) {
        return nullptr; /* unreadable now, though the search read it */
    }
    return addBuffer(buffer, path);
}

/*
 * Every accepted change, applied into buffers rather than onto disk.
 *
 * Nothing is written until you save, so the whole operation is undone
 * by closing without saving, and each file keeps its own `u`. That is
 * the trade this design made: the preview is the safety mechanism, and
 * undo is per file. See docs/adr/0131.
 */
void MainWindow::applyProjectReplace(const QString &root,
                                      const QVector<project::Replacement> &replacements,
                                      int needleLength, const QByteArray &replacement) {
    const QMap<QString, QVector<TextEdit>> byFile =
        project::editsByFile(replacements, needleLength, replacement);
    if (byFile.isEmpty()) {
        return;
    }

    /* Restored at the end: opening nine files should leave you where
     * you were, not in whichever one happened to be last. */
    const int wasActive = m_stack->currentIndex();

    int changedFiles = 0;
    int changedEdits = 0;
    int skippedFiles = 0;
    for (auto it = byFile.constBegin(); it != byFile.constEnd(); ++it) {
        EditorViewport *viewport = viewportForPath(QDir(root).filePath(it.key()));
        if (viewport == nullptr) {
            skippedFiles++;
            continue;
        }
        int applied = viewport->applyLineEdits(it.value());
        if (applied > 0) {
            changedFiles++;
            changedEdits += applied;
        }
        if (applied < it.value().size()) {
            /* A hit that no longer describes the file it came from.
             * Counted, not hidden: a replace that silently did less
             * than it said is the failure worth reporting. */
            skippedFiles++;
        }
    }

    if (wasActive >= 0 && wasActive < m_viewports.size()) {
        setActiveIndex(wasActive);
    }
    refreshBufferBar();

    QString message = QStringLiteral("%1 %2 in %3 %4 — unsaved")
                          .arg(changedEdits)
                          .arg(changedEdits == 1 ? QStringLiteral("change") : QStringLiteral("changes"))
                          .arg(changedFiles)
                          .arg(changedFiles == 1 ? QStringLiteral("file") : QStringLiteral("files"));
    if (skippedFiles > 0) {
        message += QStringLiteral("; %1 %2 skipped, changed since the search")
                       .arg(skippedFiles)
                       .arg(skippedFiles == 1 ? QStringLiteral("file") : QStringLiteral("files"));
    }
    showMessage(skippedFiles > 0 ? NotifyLevel::Warning : NotifyLevel::Info, message);
}

void MainWindow::askAboutRecovery(EditorViewport *viewport, const QString &path) {
    bool discard = confirmDestructive(
        this, viewport, QStringLiteral("Unsaved changes recovered"),
        QStringLiteral("\"%1\" has unsaved changes from a session that ended unexpectedly.")
            .arg(QFileInfo(path).fileName()),
        QStringLiteral("Discard them"), QStringLiteral("Recover"));
    if (discard) {
        viewport->discardRecovery();
    } else {
        viewport->restoreFromRecovery();
    }
}

void MainWindow::showLspState(LspState state, const QString &serverName) {
    EditorViewport *viewport = activeViewport();
    if (viewport == nullptr) {
        return;
    }
    QString text = lspLabelText(state, serverName);
    /* Skipping the highlight is a decision the editor made about this
     * file, so it says so. An unexplained lack of colour reads as a
     * broken grammar. */
    if (viewport->syntaxSkippedForSize()) {
        text = text.isEmpty() ? QStringLiteral("no highlight (large file)")
                              : QStringLiteral("%1 · no highlight (large file)").arg(text);
    }
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

/* The same file named two ways is one file. A buffer opened as
 * `ase core/src/buffer.c` keeps that relative path, while anything
 * built from the project root — a search hit, a definition, a reference
 * — is absolute, and comparing the strings said they were different
 * files. The result was a second tab for a file already on screen, and
 * every later jump landing in the duplicate rather than where you were
 * reading. Symlinks collapse here too, for the same reason.
 *
 * canonicalFilePath() is empty for a file that does not exist, which is
 * a real case: a definition in a generated header that was cleaned. */
static QString sameFileKey(const QString &path) {
    if (path.isEmpty()) {
        return QString();
    }
    QFileInfo info(path);
    QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

void MainWindow::openBuffer(const QString &path) {
    const QString wanted = sameFileKey(path);
    for (int i = 0; i < m_viewports.size(); ++i) {
        if (!wanted.isEmpty() && sameFileKey(m_viewports[i]->filePath()) == wanted) {
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
    /* A half-typed command belongs to the buffer it was typed in. */
    m_pendingLabel->clear();
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
    saveSession();
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
    /* Work you were asked about and chose to drop is not work a crash
     * took. Leaving the snapshot would offer it back at the next start,
     * having just been told to discard it. */
    viewport->discardRecovery();

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
    return confirmDestructive(this, themeSource, QStringLiteral("Unsaved changes"), message,
                              QStringLiteral("Discard"));
}

/* <config dir>/session.ase, beside config.ase. */
QString sessionPath() {
    char *config = ase_config_default_path();
    if (config == nullptr) {
        return QString();
    }
    QString dir = QFileInfo(QString::fromLocal8Bit(config)).dir().path();
    free(config);
    return QDir(dir).filePath(QStringLiteral("session.ase"));
}

/* Only what can be reopened: an unnamed buffer has no path to record,
 * and ase_session_add drops it. */
void MainWindow::saveSession() {
    /* restoreSession() adds buffers one at a time; writing after each
     * would record a session half the size of the one being restored. */
    if (m_restoringSession) {
        return;
    }
    QString path = sessionPath();
    if (path.isEmpty()) {
        return;
    }

    AseSession *session = ase_session_create();
    if (session == nullptr) {
        return;
    }
    int active = 0;
    QString signature;
    for (EditorViewport *viewport : m_viewports) {
        if (viewport->filePath().isEmpty()) {
            continue;
        }
        if (viewport == m_stack->currentWidget()) {
            active = static_cast<int>(ase_session_count(session));
        }
        ase_session_add(session, viewport->filePath().toUtf8().constData(),
                         viewport->cursorOffset(), viewport->scrollLine());
        signature += QStringLiteral("%1:%2:%3|")
                         .arg(viewport->filePath())
                         .arg(viewport->cursorOffset())
                         .arg(viewport->scrollLine());
    }
    ase_session_set_active(session, static_cast<size_t>(active));
    signature += QString::number(active);

    /* The timer below asks every few seconds whether anything moved.
     * Usually nothing has, and an unchanged session is not worth an
     * fsync. */
    if (signature != m_lastSessionSignature) {
        ase_session_save(session, path.toUtf8().constData());
        m_lastSessionSignature = signature;
    }
    ase_session_destroy(session);
}

/* Reopens what was there, skipping anything that has since been moved
 * or deleted: a session should not resurrect a file that no longer
 * exists, nor refuse to start because one is missing. Returns false when
 * nothing was reopened, so the caller can fall back to a fresh buffer. */
bool MainWindow::restoreSession() {
    QString path = sessionPath();
    if (path.isEmpty()) {
        return false;
    }
    /* Opt-out: reopening yesterday's files is the right default but the
     * wrong behaviour for someone who wants a clean window every time. */
    char *configPath = ase_config_default_path();
    if (configPath != nullptr) {
        AseConfig *config = ase_config_load(configPath);
        free(configPath);
        if (config != nullptr) {
            const char *value = ase_config_get_string(config, "restore_session");
            bool enabled = value == nullptr || strcmp(value, "false") != 0;
            ase_config_destroy(config);
            if (!enabled) {
                return false;
            }
        }
    }
    AseSession *session = ase_session_load(path.toUtf8().constData());
    if (session == nullptr) {
        return false;
    }
    m_restoringSession = true;

    size_t wanted = ase_session_active(session);
    int activeIndex = -1;
    for (size_t i = 0; i < ase_session_count(session); i++) {
        const AseSessionEntry *entry = ase_session_entry(session, i);
        QByteArray utf8 = QByteArray(entry->path);
        if (!QFileInfo::exists(QString::fromUtf8(utf8))) {
            continue;
        }
        AseBuffer *reopened = ase_buffer_create_from_file(utf8.constData());
        if (reopened == nullptr) {
            continue;
        }
        EditorViewport *viewport = addBuffer(reopened, QString::fromUtf8(utf8));
        /* Queued, not applied here: addBuffer runs before the window is
         * shown, so the viewport has no real height yet and the
         * ensureCursorVisible inside restorePosition would scroll
         * against a bogus one. */
        QPointer<EditorViewport> pending = viewport;
        size_t cursor = entry->cursor;
        int scroll = static_cast<int>(entry->scroll_line);
        QTimer::singleShot(0, this, [pending, cursor, scroll]() {
            if (!pending.isNull()) {
                pending->restorePosition(cursor, scroll);
            }
        });
        if (i == wanted) {
            activeIndex = m_viewports.size() - 1;
        }
    }
    ase_session_destroy(session);
    m_restoringSession = false;

    if (m_viewports.isEmpty()) {
        return false;
    }
    /* The remembered buffer may have been one of the missing ones. */
    setActiveIndex(activeIndex >= 0 ? activeIndex : 0);
    return true;
}

/*
 * The prefix, caught before anything else sees it.
 *
 * An application-wide filter and not a viewport handler, because the
 * whole point is reaching the editor *from* the panel: a key that only
 * the editor can hear cannot be the way back to it. The same filter is
 * why the prefix works while a find bar or the file browser holds
 * focus. See docs/adr/0120.
 */
bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress) {
        return QMainWindow::eventFilter(watched, event);
    }
    auto *key = static_cast<QKeyEvent *>(event);
    /* A modifier on its own is not the second key of anything. */
    if (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift ||
        key->key() == Qt::Key_Alt || key->key() == Qt::Key_Meta) {
        return QMainWindow::eventFilter(watched, event);
    }

    const AseConfig *config = nullptr;
    if (EditorViewport *viewport = activeViewport()) {
        config = viewport->config();
    }
    const QString chord = keys::chordFor(key);

    if (!m_pendingPrefix.isEmpty()) {
        const QString pending = m_pendingPrefix;
        m_pendingPrefix.clear();
        hideMessage();
        if (key->key() == Qt::Key_Escape) {
            return true; /* cancelled, and the Escape is spent doing it */
        }
        const QString command = keys::commandForSequence(config, pending, chord);
        if (command.isEmpty() || command == QLatin1String("none")) {
            showMessage(NotifyLevel::Warning, QStringLiteral("%1 is not bound")
                                                   .arg(keys::pretty(pending + keys::kSequenceSeparator + chord)));
            return true;
        }
        if (!m_commands.run(command)) {
            if (EditorViewport *viewport = activeViewport()) {
                viewport->runNamedCommand(command);
            }
        }
        return true;
    }

    if (!chord.isEmpty() && keys::isPrefix(config, chord)) {
        m_pendingPrefix = chord;
        /* Say what it is waiting for. A prefix that swallows a keystroke
         * and shows nothing is indistinguishable from a dropped key. */
        QStringList options;
        for (const QString &next : keys::sequenceHints(config, chord)) {
            options << keys::pretty(next);
        }
        showMessage(NotifyLevel::Info, QStringLiteral("%1 \u2192  %2")
                                            .arg(keys::pretty(chord), options.join(QStringLiteral("  "))));
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_forceClose) {
        saveSession();
        discardEveryRecovery();
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
        saveSession();
        discardEveryRecovery();
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
        saveSession();
        discardEveryRecovery();
        event->accept();
    } else {
        event->ignore();
    }
}

/* A clean exit is not a crash. Anything still on disk after this one is
 * something the editor did not get to finish. */
void MainWindow::discardEveryRecovery() {
    for (EditorViewport *viewport : m_viewports) {
        viewport->discardRecovery();
    }
}
} // namespace

namespace {

QString mdEscape(const QString &text) {
    QString out = text;
    out.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    return out;
}

/* "ctrl+w>j" reads as "Ctrl+W then J". */
QString prettySequence(const QString &chord) {
    QStringList parts;
    for (const QString &half : chord.split(QLatin1Char(keys::kSequenceSeparator))) {
        parts << keys::pretty(half);
    }
    return parts.join(QStringLiteral(" then "));
}

QString modeLabel(const QString &mode) {
    return mode.isEmpty() ? QStringLiteral("any") : mode;
}

bool writeFile(const QString &path, const QString &body) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << body;
    return true;
}

const char *kGeneratedNote =
    "<!-- Generated by `ase --dump-docs`. Do not edit: your changes will be\n"
    "     overwritten the next time the docs are built. -->\n\n";

} // namespace

/*
 * The three Reference pages, written from the tables the editor is
 * actually running on rather than from a description of them. A
 * hand-written copy of any of these is wrong the first time a command
 * is renamed, and nothing would say so. See docs/adr/0126.
 */
bool MainWindow::dumpDocs(const QString &outDir) {
    QDir dir(outDir);
    if (!dir.exists() && !QDir().mkpath(outDir)) {
        fprintf(stderr, "ase: cannot create %s\n", qPrintable(outDir));
        return false;
    }

    EditorViewport *viewport = activeViewport();
    if (viewport == nullptr) {
        fprintf(stderr, "ase: no buffer to read commands from\n");
        return false;
    }

    /* Which keys reach a command, so the command list can say. */
    QHash<QString, QStringList> keysByCommand;
    for (const keys::Binding &binding : keys::defaults()) {
        QString label = prettySequence(QString::fromLatin1(binding.chord));
        if (*binding.mode != '\0') {
            label += QStringLiteral(" (%1)").arg(QLatin1String(binding.mode));
        }
        keysByCommand[QString::fromLatin1(binding.command)] << label;
    }

    /* ---- commands ---- */
    QString body = QStringLiteral("# Every command\n\n");
    body += QLatin1String(kGeneratedNote);
    body += QStringLiteral(
        "Every action the editor can be asked to perform, under a name. These are\n"
        "the names the right-hand side of a `key.` setting takes — see\n"
        "[Keybindings](../guide/keybindings.md).\n\n"
        "Each can also be typed on the `:` line — `:editor.find` — alongside the\n"
        "ex-commands listed in [The command line](../guide/command-line.md). Both\n"
        "paths resolve a name the same way. See docs/adr/0128.\n\n");

    struct Group {
        const char *prefix;
        const char *title;
        const char *blurb;
    };
    const Group groups[] = {
        {"editor.", "Editor", "Act on the buffer you are in."},
        {"vim.", "Vim", "Bound in Normal and Visual mode only."},
        {"buffer.", "Buffers", "Act on the list of open files."},
        {"pane.", "Panes", "Act on the regions of the window."},
    };

    QStringList names = viewport->ownCommands().names() + m_commands.names();
    names.sort();
    names.removeDuplicates();

    QStringList ungrouped = names;
    for (const Group &group : groups) {
        QStringList inGroup;
        for (const QString &name : names) {
            if (name.startsWith(QLatin1String(group.prefix))) {
                inGroup << name;
            }
        }
        if (inGroup.isEmpty()) {
            continue;
        }
        body += QStringLiteral("## %1\n\n%2\n\n").arg(QLatin1String(group.title),
                                                       QLatin1String(group.blurb));
        body += QStringLiteral("| Command | Does | Default keys |\n| --- | --- | --- |\n");
        for (const QString &name : inGroup) {
            QString description = viewport->ownCommands().describe(name);
            if (description.isEmpty()) {
                description = m_commands.describe(name);
            }
            QStringList bound = keysByCommand.value(name);
            bound.removeDuplicates();
            body += QStringLiteral("| `%1` | %2 | %3 |\n")
                        .arg(name, mdEscape(description),
                             bound.isEmpty() ? QStringLiteral("—")
                                             : mdEscape(bound.join(QStringLiteral(", "))));
        }
        body += QLatin1Char('\n');
        for (const QString &name : inGroup) {
            ungrouped.removeAll(name);
        }
    }
    if (!ungrouped.isEmpty()) {
        fprintf(stderr, "ase: no group for %s in dumpDocs()\n",
                qPrintable(ungrouped.join(QStringLiteral(", "))));
        return false;
    }
    if (!writeFile(dir.filePath(QStringLiteral("commands.md")), body)) {
        return false;
    }

    /* ---- keybindings ---- */
    body = QStringLiteral("# Every default binding\n\n");
    body += QLatin1String(kGeneratedNote);
    body += QStringLiteral(
        "The built-in table in full. Any row can be replaced in your config — see\n"
        "[Keybindings](../guide/keybindings.md) for how, and for why the layers are\n"
        "shaped this way.\n\n"
        "`Mode` is `any` unless the row only applies in one Vim mode.\n\n");

    struct Layer {
        const char *title;
        const char *blurb;
        bool (*matches)(const QString &);
    };
    const Layer layers[] = {
        {"Ctrl", "Brought from other editors and the OS, plus Vim's own Ctrl chords.",
         [](const QString &c) {
             return c.startsWith(QLatin1String("ctrl+")) && !c.contains(QLatin1Char('>'));
         }},
        {"Ctrl+W", "Structure: regions, their focus, size and life. Vim's window prefix.",
         [](const QString &c) { return c.contains(QLatin1Char('>')); }},
        {"Alt", "This editor's own, where there is no convention to inherit.",
         [](const QString &c) { return c.startsWith(QLatin1String("alt+")); }},
        {"Function keys", "Ask the language server.",
         [](const QString &c) { return c.contains(QLatin1String("f1")) || c.contains(QLatin1String("f12")); }},
    };

    /* A row matching no layer would simply not be written, and the page
     * would look complete. Counted instead. */
    int written = 0;
    for (const Layer &layer : layers) {
        QStringList rows;
        for (const keys::Binding &binding : keys::defaults()) {
            QString chord = QString::fromLatin1(binding.chord);
            if (!layer.matches(chord)) {
                continue;
            }
            rows << QStringLiteral("| %1 | `%2` | %3 |")
                        .arg(prettySequence(chord), QString::fromLatin1(binding.command),
                             modeLabel(QString::fromLatin1(binding.mode)));
        }
        if (rows.isEmpty()) {
            continue;
        }
        body += QStringLiteral("## %1\n\n%2\n\n").arg(QLatin1String(layer.title),
                                                       QLatin1String(layer.blurb));
        body += QStringLiteral("| Keys | Command | Mode |\n| --- | --- | --- |\n");
        body += rows.join(QLatin1Char('\n'));
        body += QStringLiteral("\n\n");
        written += rows.size();
    }
    if (written != keys::defaults().size()) {
        fprintf(stderr,
                "ase: %d of %lld default bindings matched no layer in dumpDocs(); "
                "add one rather than shipping a page that is quietly short\n",
                static_cast<int>(keys::defaults().size()) - written,
                static_cast<long long>(keys::defaults().size()));
        return false;
    }
    if (!writeFile(dir.filePath(QStringLiteral("keybindings.md")), body)) {
        return false;
    }

    /* ---- config keys ---- */
    body = QStringLiteral("# Every config key\n\n");
    body += QLatin1String(kGeneratedNote);
    body += QStringLiteral(
        "One `key = value` per line in `~/.config/ase/config.ase`. See\n"
        "[Configuration](../guide/configuration.md) for the file's shape and how it\n"
        "reloads.\n\n"
        "A key written `<like this>` stands for a family: substitute the part in\n"
        "angle brackets.\n\n"
        "A key marked **project** can also be set in a `.ase.conf` at the root of a\n"
        "project, which only that project reads.\n\n"
        "| Key | Default | Means | Project |\n| --- | --- | --- | --- |\n");

    size_t count = 0;
    const AseConfigKeyDoc *docs = ase_config_key_docs(&count);
    for (size_t i = 0; i < count; i++) {
        QString value = docs[i].value == nullptr ? QStringLiteral("*unset*")
                                                  : QStringLiteral("`%1`").arg(
                                                        QString::fromUtf8(docs[i].value));
        body += QStringLiteral("| `%1` | %2 | %3 | %4 |\n")
                    .arg(QString::fromUtf8(docs[i].key), value,
                         mdEscape(QString::fromUtf8(docs[i].summary)),
                         docs[i].project ? QStringLiteral("yes") : QStringLiteral("—"));
    }
    body += QStringLiteral(
        "\n## Built-in themes\n\n"
        "`theme = <name>`, or `:theme <name>` to see it before you keep it. A theme\n"
        "sets the nine colours above; anything you set by hand stays yours.\n\n"
        "| Name | Is |\n| --- | --- |\n");
    for (size_t i = 0; i < ase_theme_count(); i++) {
        const AseTheme *theme = ase_theme_at(i);
        body += QStringLiteral("| `%1` | %2 |\n")
                    .arg(QString::fromUtf8(theme->name),
                         mdEscape(QString::fromUtf8(theme->summary)));
    }
    if (!writeFile(dir.filePath(QStringLiteral("config.md")), body)) {
        return false;
    }

    return true;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    /* Bundled via Qt resources; reused in the About panel. */
    app.setWindowIcon(QIcon(QStringLiteral(":/ase.png")));

    /* The panel fields draw their own caret now, but anything native
     * appearing later still blinks on the editor's cadence rather than
     * the platform's ~1000ms default. */
    app.setCursorFlashTime(720);

    /* Writes the generated Reference pages and exits. Needs a real
     * window and a real buffer, because the command tables only exist
     * once those are built. */
    if (argc == 3 && QLatin1String(argv[1]) == QLatin1String("--dump-docs")) {
        MainWindow window;
        window.addBuffer(ase_buffer_create(), QString());
        return window.dumpDocs(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
    }

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

    /* Only when launched bare: `ase foo.c` means foo.c, not "and the
     * eleven things I had open last week". See docs/adr/0111. */
    bool restored = false;
    if (filePath.isEmpty()) {
        restored = window.restoreSession();
    }

    if (restored) {
        /* The placeholder buffer nothing opened into. */
        ase_buffer_destroy(buffer);
    } else {
        /* Launching with no file is the only situation the welcome
         * greeting belongs to, and only here is that knowable: a Ctrl+N
         * buffer looks identical to the viewport. See docs/adr/0058. */
        EditorViewport *first = window.addBuffer(buffer, filePath);
        if (filePath.isEmpty()) {
            first->armWelcomeGreeting();
        }
    }
    window.show();

    /* After show(), so the prompt has a window to sit over, and after
     * the session so a recovered buffer joins what is already open. */
    window.recoverUntitledBuffers();

    return app.exec();
}
