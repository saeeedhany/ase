/*
 * A compiler's complaints, put where the code is — see docs/adr/0148.
 *
 * The parsing itself is core's (core/tests/test_build.c). This is the
 * half that has a buffer to check against: what a point turns into,
 * what happens to a line that no longer exists, and the two producers
 * not clearing each other.
 */
#include "editor_viewport.h"

#include "ase/buffer.h"

#include <QApplication>
#include <QTest>

namespace {

AseBuffer *bufferFrom(const QByteArray &text) {
    AseBuffer *buffer = ase_buffer_create();
    if (!text.isEmpty()) {
        ase_buffer_insert(buffer, 0, text.constData(), static_cast<size_t>(text.size()));
    }
    return buffer;
}

GuiDiagnostic at(int line, int column, int severity, const QString &message) {
    GuiDiagnostic d;
    d.startLine = line;
    d.startChar = column;
    d.endLine = line;
    d.endChar = -1; /* a compiler names a point, not a range */
    d.severity = severity;
    d.message = message;
    d.source = GuiDiagnostic::Source::Build;
    return d;
}

} // namespace

class BuildDiagnostics : public QObject {
    Q_OBJECT

private slots:
    /* A compiler gives a line and a column. The mark has to run to the
     * end of the line, and only this side knows how long that is. */
    void aPointBecomesARangeToTheEndOfTheLine() {
        AseBuffer *buffer = bufferFrom("short\na much longer line\n");
        EditorViewport viewport(buffer, QString());

        viewport.applyBuildDiagnostics({at(0, 2, 1, QStringLiteral("boom"))});
        QCOMPARE(viewport.diagnostics().size(), 1);
        QCOMPARE(viewport.diagnostics().at(0).endChar, 5); /* "short" */

        viewport.applyBuildDiagnostics({at(1, 0, 1, QStringLiteral("boom"))});
        QCOMPARE(viewport.diagnostics().at(0).endChar, 18); /* the longer one */
    }

    /* Editing after a build moves the code out from under the marks. A
     * line that no longer exists is dropped rather than clamped onto
     * whatever is nearest. */
    void aLinePastTheEndIsDropped() {
        AseBuffer *buffer = bufferFrom("one\ntwo\n");
        EditorViewport viewport(buffer, QString());

        viewport.applyBuildDiagnostics({at(0, 0, 1, QStringLiteral("real")),
                                         at(99, 0, 1, QStringLiteral("gone")),
                                         at(-1, 0, 1, QStringLiteral("also gone"))});
        QCOMPARE(viewport.diagnostics().size(), 1);
        QCOMPARE(viewport.diagnostics().at(0).message, QStringLiteral("real"));
    }

    /* A build replaces its own findings and nothing else's. */
    void aSecondBuildReplacesTheFirst() {
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());

        viewport.applyBuildDiagnostics({at(0, 0, 1, QStringLiteral("first")),
                                         at(1, 0, 2, QStringLiteral("second"))});
        QCOMPARE(viewport.diagnostics().size(), 2);

        viewport.applyBuildDiagnostics({at(2, 0, 1, QStringLiteral("third"))});
        QCOMPARE(viewport.diagnostics().size(), 1);
        QCOMPARE(viewport.diagnostics().at(0).message, QStringLiteral("third"));

        viewport.clearBuildDiagnostics();
        QCOMPARE(viewport.diagnostics().size(), 0);
    }

    /* The severity the compiler used survives, because the gutter
     * colours by it. */
    void severityIsCarriedThrough() {
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());

        viewport.applyBuildDiagnostics({at(0, 0, 1, QStringLiteral("e")),
                                         at(1, 0, 2, QStringLiteral("w")),
                                         at(2, 0, 3, QStringLiteral("n"))});
        QVector<GuiDiagnostic> got = viewport.diagnostics();
        QCOMPARE(got.size(), 3);
        QCOMPARE(got.at(0).severity, 1);
        QCOMPARE(got.at(1).severity, 2);
        QCOMPARE(got.at(2).severity, 3);
        for (const GuiDiagnostic &d : got) {
            QCOMPARE(d.source, GuiDiagnostic::Source::Build);
        }
    }

    /* An empty line has no characters to underline and still has to be
     * markable — a missing semicolon is often reported on one. */
    void anEmptyLineStillGetsAMark() {
        AseBuffer *buffer = bufferFrom("one\n\nthree\n");
        EditorViewport viewport(buffer, QString());

        viewport.applyBuildDiagnostics({at(1, 0, 1, QStringLiteral("here"))});
        QCOMPARE(viewport.diagnostics().size(), 1);
        QVERIFY(viewport.diagnostics().at(0).endChar > viewport.diagnostics().at(0).startChar);
    }
};

QTEST_MAIN(BuildDiagnostics)
#include "test_build_diagnostics.moc"
