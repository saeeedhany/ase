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
/* Shared by the initial title and every statusChanged-driven update —
 * see docs/adr/0023. */
QString windowTitleFor(const QString &filePath, bool dirty) {
    QString name = filePath.isEmpty() ? QStringLiteral("untitled") : filePath;
    return (dirty ? QStringLiteral("%1 [modified] — Absolute Simple Editor") : QStringLiteral("%1 — Absolute Simple Editor"))
        .arg(name);
}

/* Just the file name for the buffer bar — the full path is already in
 * the window title, and a bar of long paths would defeat the point of
 * keeping it minimal (docs/adr/0054). */
QString bufferLabelFor(const QString &filePath) {
    if (filePath.isEmpty()) {
        return QStringLiteral("untitled");
    }
    return QFileInfo(filePath).fileName();
}

/*
 * The window: owns the open buffers, one EditorViewport each, stacked so
 * exactly one is visible. See docs/adr/0054 for why a viewport per buffer
 * rather than one viewport swapping documents.
 *
 * Also still the quit-confirmation owner (docs/adr/0044) — now asking
 * about every dirty buffer, not just the visible one.
 */
class MainWindow : public QMainWindow {
public:
    MainWindow();

    /* Opens `path` as a new buffer, or switches to it if already open —
     * reopening a file you already have open should take you to it, not
     * give you a second copy to diverge from. */
    void showMessage(NotifyLevel level, const QString &text);
    void clearStickyMessage();
    void hideMessage();
    void openBuffer(const QString &path);
    /* Ctrl+N — an empty, pathless buffer. Saving it routes through
     * Save-As, since save() already sends an empty path there
     * (docs/adr/0006). */
    void newBuffer();
    /* Returns the viewport it created, which main() needs so it can
     * arm the welcome greeting on the startup buffer. */
    EditorViewport *addBuffer(AseBuffer *buffer, const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    EditorViewport *activeViewport() const;
    void setActiveIndex(int index);
    void closeBuffer(int index);
    void cycleBuffer(int delta);
    void refreshBufferBar();
    bool confirmDiscard(const QString &message);

    QStackedWidget *m_stack = nullptr;
    BufferBar *m_bufferBar = nullptr;
    OutputPanel *m_outputPanel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    /* The message area — see showMessage() and docs/adr/0062. Shares the
     * bar with the mode label rather than replacing it: losing NORMAL
     * because a file got saved would be a bad trade. */
    QLabel *m_messageLabel = nullptr;
    QGraphicsOpacityEffect *m_messageOpacity = nullptr;
    QPropertyAnimation *m_messageFade = nullptr;
    QTimer *m_messageTimer = nullptr;
    /* An error stays up until you do something else — see
     * clearStickyMessage(). Armed one event-loop turn after it is shown,
     * so the very keystroke that produced it cannot also dismiss it. */
    bool m_messageSticky = false;
    bool m_messageStickyArmed = false;
    QVector<EditorViewport *> m_viewports;
};

/* QStatusBar defaults to a native, light OS-styled bar — jarring
 * against this app's dark, minimal palette, same problem the floating
 * panels' native-white QLineEdits had (docs/adr/0022). Re-applied on
 * every statusChanged (see below), so a hot-reloaded config color
 * reaches the status bar too, not just the editor. See docs/adr/0024. */
void applyStatusBarTheme(QMainWindow &window, QLabel *modeLabel, QLabel *statusLabel, EditorViewport *viewport) {
    QPalette pal = window.statusBar()->palette();
    pal.setColor(QPalette::Window, viewport->backgroundColor());
    pal.setColor(QPalette::WindowText, viewport->textColor());
    window.statusBar()->setPalette(pal);
    window.statusBar()->setAutoFillBackground(true);
    modeLabel->setPalette(pal);
    statusLabel->setPalette(pal);
}

/* Info and warnings are the status bar's own text colour — the
 * one-font-colour pillar (docs/adr/0007) — separated by how long they
 * linger, not by hue. Info drops one opacity tier, the same "this line
 * is secondary" move the About panel and welcome screen already use
 * (docs/adr/0055): a confirmation you did not need should not shout as
 * loudly as the mode label it sits beside. A warning is something you
 * are meant to read, so it does not drop.
 *
 * Errors are the exception the theme already sanctions:
 * `diagnostic_error`, the same colour as the gutter dot and the
 * underline, so nothing new enters the palette. */
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

/* How long a message sits before fading, in milliseconds.
 *
 * Info is a glance: long enough to catch if you look down, short enough
 * that it is gone before it becomes furniture. A warning is a sentence
 * you are meant to finish reading, and it is usually telling you the
 * thing you just asked for did not happen.
 *
 * An error does not time out at all — see clearStickyMessage(). */
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
    /* The buffer bar sits above the editor and the output panel below it
     * — both plain layout rows, not floating chrome. OutputPanel is
     * shared across buffers rather than one per viewport: it shows the
     * result of the last :compile, which is a property of the window,
     * not of whichever file you happen to be looking at. */
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    m_bufferBar = new BufferBar(central);
    centralLayout->addWidget(m_bufferBar);

    m_stack = new QStackedWidget(central);
    centralLayout->addWidget(m_stack, 1);

    m_outputPanel = new OutputPanel(nullptr, central);
    centralLayout->addWidget(m_outputPanel);
    setCentralWidget(central);

    connect(m_bufferBar, &BufferBar::bufferSelected, this, [this](int index) { setActiveIndex(index); });
    connect(m_bufferBar, &BufferBar::bufferCloseRequested, this, [this](int index) { closeBuffer(index); });
    connect(m_bufferBar, &BufferBar::newBufferRequested, this, [this]() { newBuffer(); });

    statusBar()->setSizeGripEnabled(false);
    /* addWidget (not addPermanentWidget) puts this in the status bar's
     * left-aligned message area, separate from statusLabel's own
     * right-aligned permanent slot below. Empty whenever Vim mode is
     * off (docs/adr/0046), so it takes no visible space for anyone who
     * hasn't opted in. */
    m_modeLabel = new QLabel();
    statusBar()->addWidget(m_modeLabel);
    /* Also addWidget, so it sits immediately right of the mode label and
     * grows into the free middle of the bar — the space that was doing
     * nothing. Elided rather than allowed to push the position readout
     * off the edge on a narrow window. */
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
    m_statusLabel = new QLabel(QStringLiteral("Ln 1, Col 1"));
    statusBar()->addPermanentWidget(m_statusLabel);

    /* Window-level shortcuts, deliberately not part of EditorViewport's
     * own Ctrl-chain: they act on the *window's* buffer list, not on the
     * text. Qt dispatches shortcuts before the focus widget's key
     * handler, so Ctrl+Tab never reaches the viewport's Tab case. */
    auto *next = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this);
    connect(next, &QShortcut::activated, this, [this]() { cycleBuffer(1); });
    auto *prev = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this);
    connect(prev, &QShortcut::activated, this, [this]() { cycleBuffer(-1); });
    auto *close = new QShortcut(QKeySequence(QStringLiteral("Ctrl+W")), this);
    connect(close, &QShortcut::activated, this, [this]() { closeBuffer(m_stack->currentIndex()); });
    auto *newFile = new QShortcut(QKeySequence(QStringLiteral("Ctrl+N")), this);
    connect(newFile, &QShortcut::activated, this, [this]() { newBuffer(); });
}

EditorViewport *MainWindow::activeViewport() const {
    int index = m_stack->currentIndex();
    return (index >= 0 && index < m_viewports.size()) ? m_viewports[index] : nullptr;
}

EditorViewport *MainWindow::addBuffer(AseBuffer *buffer, const QString &path) {
    auto *viewport = new EditorViewport(buffer, path);

    /* FindBar, FileBrowserPanel, CommandLine, Help and About are all
     * FloatingPanels: children of *this* viewport, not layout rows, each
     * centering itself over its own host (docs/adr/0022). So every
     * buffer gets its own set — they're inert until shown, and sharing
     * one across viewports would mean a panel floating over the wrong
     * parent. */
    viewport->setFindBar(new FindBar(viewport));
    viewport->setFileBrowser(new FileBrowserPanel(viewport));
    viewport->setCommandLine(new CommandLine(viewport));
    viewport->setHelpPanel(new HelpPanel(viewport));
    viewport->setAboutPanel(new AboutPanel(viewport));
    /* Not FloatingPanels either — see completion_popup.h/hover_panel.h.
     * Both track the caret/pointer and refresh far more often than a
     * glance-act-dismiss chrome window. */
    viewport->setCompletionPopup(new CompletionPopup(viewport));
    viewport->setHoverPanel(new HoverPanel(viewport));
    viewport->setOutputPanel(m_outputPanel);

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
                refreshBufferBar(); /* the name may have changed via Save-As */
            });
    connect(viewport, &EditorViewport::fileOpenRequested, this,
            [this](const QString &path) { openBuffer(path); });
    /* Only the buffer you are looking at gets to speak — same rule the
     * status readout follows, and for the same reason: a message about a
     * file in another tab, with nothing naming that file, reads as a
     * message about this one. */
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
void MainWindow::showMessage(NotifyLevel level, const QString &text) {
    EditorViewport *viewport = activeViewport();
    if (viewport == nullptr) {
        return;
    }

    QPalette pal = m_messageLabel->palette();
    pal.setColor(QPalette::WindowText, messageColorFor(level, viewport));
    m_messageLabel->setPalette(pal);
    /* Elided here rather than by the layout, so a long path shortens the
     * message instead of squeezing Ln/Col off the end of the bar. */
    QFontMetrics metrics(m_messageLabel->font());
    int room = std::max(80, m_messageLabel->width());
    m_messageLabel->setText(metrics.elidedText(text, Qt::ElideMiddle, room));
    m_messageLabel->setToolTip(text);

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
        /* Next turn, not now: the keystroke that caused this error is
         * still being delivered, and anything it emits on its way out
         * would otherwise dismiss the error before it was read. */
        QTimer::singleShot(0, this, [this]() { m_messageStickyArmed = true; });
    }
}

void MainWindow::hideMessage() {
    m_messageSticky = false;
    m_messageStickyArmed = false;
    m_messageTimer->stop();
    m_messageFade->stop();
    m_messageFade->setStartValue(m_messageOpacity->opacity());
    m_messageFade->setEndValue(0.0);
    m_messageFade->start();
}

/*
 * Vim's rule: an error sits on the message line until you do something
 * else, then it is gone. "Something else" here is any cursor move or
 * edit — statusChanged already fires on exactly those and on nothing
 * that isn't the user (docs/adr/0023), so this needs no new signal and
 * cannot miss a case that one would.
 *
 * The alternative, letting an error time out like the other tiers, was
 * rejected: the one message you must not miss is the one saying a thing
 * you asked for did not happen, and a save that failed while you were
 * looking elsewhere is exactly how people lose work.
 */
void MainWindow::clearStickyMessage() {
    if (m_messageSticky && m_messageStickyArmed) {
        hideMessage();
    }
}

void MainWindow::openBuffer(const QString &path) {
    for (int i = 0; i < m_viewports.size(); ++i) {
        if (m_viewports[i]->filePath() == path) {
            setActiveIndex(i);
            return;
        }
    }

    /* Missing/unreadable files start empty with `path` kept as the save
     * target — opening a not-yet-existing file by name is a normal
     * editor action, not an error (docs/adr/0006). */
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
    /* A message raised by the buffer you just left, still sitting in the
     * bar over a different file's text, reads as being about *this*
     * file. Switching buffers clears it. */
    hideMessage();
    EditorViewport *viewport = m_viewports[index];
    m_outputPanel->setViewport(viewport);
    viewport->onActivated(); /* focuses, and starts its LSP the first time */
    refreshBufferBar();
    /* Pushes this buffer's own line/col/dirty/mode into the status bar
     * and title through the same statusChanged path every edit uses —
     * no second copy of the formatting. */
    viewport->emitInitialStatus();
}

void MainWindow::closeBuffer(int index) {
    if (index < 0 || index >= m_viewports.size()) {
        return;
    }
    /* Closing the last buffer is closing the window — going through
     * close() rather than deleting it keeps the unsaved-changes
     * confirmation in exactly one place (closeEvent below). */
    if (m_viewports.size() == 1) {
        close();
        return;
    }

    EditorViewport *viewport = m_viewports[index];
    if (viewport->isDirty() &&
        !confirmDiscard(QStringLiteral("\"%1\" has unsaved changes. Close it anyway?")
                             .arg(bufferLabelFor(viewport->filePath())))) {
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
        /* The viewport pointer is a stable, unique handle for as long as
         * the buffer is open — exactly what tab identity needs, and
         * unlike the display name it cannot collide (every `+` buffer is
         * called "untitled"). See docs/adr/0057. */
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

/* Every confirm in this window goes through here so they all look like
 * the rest of the app rather than like the OS. A plain
 * QMessageBox::question/warning() renders with the native palette and a
 * colored icon — jarring against this app's flat, dark, single-accent
 * chrome (docs/adr/0022's panels, the re-themed QStatusBar above), and
 * a reported complaint the first time it shipped (docs/adr/0044). Built
 * manually so a stylesheet can be applied before showing; NoIcon drops
 * the colored triangle, consistent with the "one font color" pillar
 * (docs/adr/0007). */
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
    /* The app's logo (gui/resources/ase.png, bundled via Qt resources
     * — see gui/resources/resources.qrc) as the window/taskbar icon,
     * and shown again inside the About panel. See docs/adr/0027. */
    app.setWindowIcon(QIcon(QStringLiteral(":/ase.png")));

    /* Kept as the floor for any *native* caret in the app. The panel
     * fields no longer have one — they are SmoothLineEdits now, drawing
     * the editor's own gliding, breathing caret (docs/adr/0058), which
     * is what this line was an approximation of: ADR 0028 could only
     * match the blink *rate* through this one global lever, and said
     * so. Anything native that appears later (a Qt dialog, a widget
     * nobody has restyled yet) still blinks on the editor's cadence
     * rather than the platform's ~1000ms default, which is the whole
     * reason to keep it. */
    app.setCursorFlashTime(720);

    QString filePath;
    AseBuffer *buffer;

    if (argc > 1) {
        filePath = QString::fromLocal8Bit(argv[1]);
        buffer = ase_buffer_create_from_file(argv[1]);
        if (buffer == nullptr) {
            /* Doesn't exist yet, or couldn't be read — start empty and
             * keep filePath as the Ctrl+S save target, like a plain-text
             * editor opening a new file by name. v1 doesn't distinguish
             * "new file" from "read error"; see docs/adr/0006. */
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
    /* Creating the first buffer also sets the title, status bar and
     * bar through the same path every later buffer takes. The one thing
     * that *is* special about it: being launched with no file at all is
     * the only situation the welcome greeting belongs to, and that fact
     * lives here, where argv is read. A pathless buffer made later with
     * Ctrl+N looks identical to the viewport and must not be greeted —
     * you are already working by then. See docs/adr/0058. */
    EditorViewport *first = window.addBuffer(buffer, filePath);
    if (filePath.isEmpty()) {
        first->armWelcomeGreeting();
    }
    window.show();

    return app.exec();
}
