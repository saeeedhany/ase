/* Scaffold-only entry point: proves the GUI shell links against the
 * headless core (docs/adr/0002). The custom-painted viewport, keyboard
 * wiring, and everything else in Phase 2 (docs/ROADMAP.md) come later. */

#include <QApplication>
#include <QMainWindow>
#include <QString>

extern "C" {
#include "ase/core.h"
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle(
        QString("Absolute Simple Editor — core %1 (scaffold)").arg(ase_core_version())
    );
    window.resize(800, 600);
    window.show();

    return app.exec();
}
