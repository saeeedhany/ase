#include <QApplication>
#include <QCloseEvent>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QString>
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
    void openBuffer(const QString &path);
    void addBuffer(AseBuffer *buffer, const QString &path);

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

    statusBar()->setSizeGripEnabled(false);
    /* addWidget (not addPermanentWidget) puts this in the status bar's
     * left-aligned message area, separate from statusLabel's own
     * right-aligned permanent slot below. Empty whenever Vim mode is
     * off (docs/adr/0046), so it takes no visible space for anyone who
     * hasn't opted in. */
    m_modeLabel = new QLabel();
    statusBar()->addWidget(m_modeLabel);
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
}

EditorViewport *MainWindow::activeViewport() const {
    int index = m_stack->currentIndex();
    return (index >= 0 && index < m_viewports.size()) ? m_viewports[index] : nullptr;
}

void MainWindow::addBuffer(AseBuffer *buffer, const QString &path) {
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

    m_viewports.push_back(viewport);
    m_stack->addWidget(viewport);
    setActiveIndex(m_viewports.size() - 1);
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

void MainWindow::setActiveIndex(int index) {
    if (index < 0 || index >= m_viewports.size()) {
        return;
    }
    m_stack->setCurrentIndex(index);
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
    QVector<QString> names;
    names.reserve(m_viewports.size());
    for (EditorViewport *viewport : m_viewports) {
        names.push_back(bufferLabelFor(viewport->filePath()));
    }
    EditorViewport *viewport = activeViewport();
    if (viewport != nullptr) {
        m_bufferBar->setColors(viewport->backgroundColor(), viewport->textColor());
    }
    m_bufferBar->setEntries(names, m_stack->currentIndex());
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

    /* Every native QLineEdit inside the floating panels (FindBar,
     * FileBrowserPanel, CommandLine) otherwise blinks its own caret on
     * Qt's platform-default cadence, independent of — and usually
     * visibly out of sync with — EditorViewport's own custom-timed
     * caret. QApplication::cursorFlashTime is the one global lever Qt
     * exposes for every native text-input caret's blink cycle; set to
     * match the editor's own hard-blink cadence (~360ms per on/off
     * toggle, i.e. a 720ms full cycle — see the blink timer in
     * EditorViewport's constructor) so the whole app blinks together.
     * See docs/adr/0028. A real gap this doesn't close: Qt's native
     * caret is always a hard on/off toggle, never the smooth fade
     * EditorViewport itself can do with `animations = true` — matching
     * *that* would mean replacing every QLineEdit's own cursor
     * painting, real work saved for if the rate match alone isn't
     * enough. */
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
     * (empty, single-buffer) bar through the same path every later
     * buffer takes — nothing about the first one is special-cased. */
    window.addBuffer(buffer, filePath);
    window.show();

    return app.exec();
}
