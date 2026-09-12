#include <QApplication>
#include <QCloseEvent>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "about_panel.h"
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

/* QStatusBar defaults to a native, light OS-styled bar — jarring
 * against this app's dark, minimal palette, same problem the floating
 * panels' native-white QLineEdits had (docs/adr/0022). Re-applied on
 * every statusChanged (see below), so a hot-reloaded config color
 * reaches the status bar too, not just the editor. See docs/adr/0024. */
void applyStatusBarTheme(QMainWindow &window, QLabel *statusLabel, EditorViewport *viewport) {
    QPalette pal = window.statusBar()->palette();
    pal.setColor(QPalette::Window, viewport->backgroundColor());
    pal.setColor(QPalette::WindowText, viewport->textColor());
    window.statusBar()->setPalette(pal);
    window.statusBar()->setAutoFillBackground(true);
    statusLabel->setPalette(pal);
}

/* No Q_OBJECT, no signals/slots of its own — just one virtual override,
 * so a plain subclass defined right here needs no moc pass. See
 * docs/adr/0044: quitting (the window's own close button, Alt+F4, or
 * Ctrl+Q via EditorViewport's window()->close()) with unsaved changes
 * now asks for confirmation instead of silently discarding them. */
class MainWindow : public QMainWindow {
public:
    explicit MainWindow(EditorViewport *viewport) : m_viewport(viewport) {}

protected:
    void closeEvent(QCloseEvent *event) override {
        if (!m_viewport->isDirty()) {
            event->accept();
            return;
        }

        /* A plain QMessageBox::warning() renders with the native OS
         * palette and a colored warning icon — jarring against this
         * app's flat, dark, single-accent-color chrome everywhere
         * else (docs/adr/0022's floating-panel system, the re-themed
         * QStatusBar in applyStatusBarTheme above). Built manually
         * instead of via the static convenience function so a
         * stylesheet can be applied before showing it; NoIcon drops
         * the colored triangle, consistent with the "one font color"
         * pillar (docs/adr/0007). */
        QColor bg = m_viewport->panelBackgroundColor();
        QColor border = m_viewport->panelBorderColor();
        QColor text = m_viewport->textColor();

        QMessageBox box(this);
        box.setIcon(QMessageBox::NoIcon);
        box.setWindowTitle(QStringLiteral("Unsaved changes"));
        box.setText(QStringLiteral("This file has unsaved changes. Quit without saving?"));
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

        if (box.clickedButton() == discardButton) {
            event->accept();
        } else {
            event->ignore();
        }
    }

private:
    EditorViewport *m_viewport;
};
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

    auto *viewport = new EditorViewport(buffer, filePath);

    MainWindow window(viewport);
    window.setWindowTitle(windowTitleFor(filePath, false));

    /* OutputPanel is the one docked (non-floating) panel — a real
     * QVBoxLayout row below viewport, not a child of it. See
     * docs/adr/0025. */
    auto *central = new QWidget(&window);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(viewport, 1);
    auto *outputPanel = new OutputPanel(viewport, central);
    centralLayout->addWidget(outputPanel);
    window.setCentralWidget(central);
    viewport->setOutputPanel(outputPanel);

    /* FindBar, FileBrowserPanel, and CommandLine are all FloatingPanels:
     * children of viewport, not layout rows — each centers itself over
     * viewport and floats above it, starting hidden. See docs/adr/0022. */
    auto *findBar = new FindBar(viewport);
    viewport->setFindBar(findBar);

    auto *fileBrowser = new FileBrowserPanel(viewport);
    viewport->setFileBrowser(fileBrowser);

    auto *commandLine = new CommandLine(viewport);
    viewport->setCommandLine(commandLine);

    auto *helpPanel = new HelpPanel(viewport);
    viewport->setHelpPanel(helpPanel);

    auto *aboutPanel = new AboutPanel(viewport);
    viewport->setAboutPanel(aboutPanel);

    /* Not FloatingPanels either — see completion_popup.h/hover_panel.h.
     * Both track the caret/pointer and refresh far more often than a
     * glance-act-dismiss chrome window, so neither uses the host-
     * centering/scale-pop machinery the panels above do. */
    auto *completionPopup = new CompletionPopup(viewport);
    viewport->setCompletionPopup(completionPopup);

    auto *hoverPanel = new HoverPanel(viewport);
    viewport->setHoverPanel(hoverPanel);

    outputPanel->refreshTheme();

    window.statusBar()->setSizeGripEnabled(false);
    auto *statusLabel = new QLabel(QStringLiteral("Ln 1, Col 1"));
    window.statusBar()->addPermanentWidget(statusLabel);
    applyStatusBarTheme(window, statusLabel, viewport);

    QObject::connect(
        viewport, &EditorViewport::statusChanged, &window,
        [&window, viewport, statusLabel](int line, int column, bool dirty, const QString &modeLabel) {
            /* modeLabel is empty whenever Vim mode is off (docs/adr/0046),
             * so this prefix is a no-op for anyone who hasn't opted in. */
            QString prefix = modeLabel.isEmpty() ? QString() : modeLabel + QStringLiteral("  ");
            statusLabel->setText(QStringLiteral("%1Ln %2, Col %3%4")
                                      .arg(prefix)
                                      .arg(line)
                                      .arg(column)
                                      .arg(dirty ? QStringLiteral(" *") : QString()));
            window.setWindowTitle(windowTitleFor(viewport->filePath(), dirty));
            applyStatusBarTheme(window, statusLabel, viewport);
        });

    window.resize(900, 650);
    window.show();
    viewport->setFocus();

    return app.exec();
}
