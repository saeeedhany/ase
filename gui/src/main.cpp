#include <QApplication>
#include <QMainWindow>
#include <QString>

#include "editor_viewport.h"

extern "C" {
#include "ase/buffer.h"
}

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
    window.setWindowTitle(filePath.isEmpty() ? QStringLiteral("Absolute Simple Editor") : filePath);

    auto *viewport = new EditorViewport(buffer, filePath);
    window.setCentralWidget(viewport);
    window.resize(900, 650);
    window.show();
    viewport->setFocus();

    return app.exec();
}
