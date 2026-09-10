#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QString>

#include "editor_viewport.h"
#include "file_browser_panel.h"
#include "find_bar.h"

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
    window.setCentralWidget(viewport);

    /* FindBar and FileBrowserPanel are both FloatingPanels: children of
     * viewport, not layout rows — each centers itself over viewport and
     * floats above it, starting hidden. See docs/adr/0022. */
    auto *findBar = new FindBar(viewport);
    viewport->setFindBar(findBar);

    auto *fileBrowser = new FileBrowserPanel(viewport);
    viewport->setFileBrowser(fileBrowser);

    auto *statusLabel = new QLabel(QStringLiteral("Ln 1, Col 1"));
    window.statusBar()->addWidget(statusLabel);

    QObject::connect(viewport, &EditorViewport::statusChanged, &window,
                      [&window, viewport, statusLabel](int line, int column, bool dirty) {
                          statusLabel->setText(QStringLiteral("Ln %1, Col %2%3")
                                                    .arg(line)
                                                    .arg(column)
                                                    .arg(dirty ? QStringLiteral(" *") : QString()));
                          window.setWindowTitle(windowTitleFor(viewport->filePath(), dirty));
                      });

    window.resize(900, 650);
    window.show();
    viewport->setFocus();

    return app.exec();
}
