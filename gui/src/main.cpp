#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QPalette>
#include <QStatusBar>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "command_line.h"
#include "editor_viewport.h"
#include "file_browser_panel.h"
#include "find_bar.h"
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
} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

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

    QMainWindow window;
    window.setWindowTitle(windowTitleFor(filePath, false));

    auto *viewport = new EditorViewport(buffer, filePath);

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

    outputPanel->refreshTheme();

    window.statusBar()->setSizeGripEnabled(false);
    auto *statusLabel = new QLabel(QStringLiteral("Ln 1, Col 1"));
    window.statusBar()->addPermanentWidget(statusLabel);
    applyStatusBarTheme(window, statusLabel, viewport);

    QObject::connect(viewport, &EditorViewport::statusChanged, &window,
                      [&window, viewport, statusLabel](int line, int column, bool dirty) {
                          statusLabel->setText(QStringLiteral("Ln %1, Col %2%3")
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
