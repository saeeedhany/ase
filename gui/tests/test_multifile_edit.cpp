/*
 * Applying a set of edits to one buffer, which is the half of a
 * multi-file edit that can go wrong quietly: every edit shifts the
 * offsets of the ones after it. See docs/adr/0131.
 */
#include "editor_viewport.h"
#include "project_edit.h"

#include "ase/buffer.h"

#include <QApplication>
#include <QKeyEvent>
#include <QTest>

namespace {

AseBuffer *bufferFrom(const QByteArray &text) {
    AseBuffer *buffer = ase_buffer_create();
    if (!text.isEmpty()) {
        ase_buffer_insert(buffer, 0, text.constData(), static_cast<size_t>(text.size()));
    }
    return buffer;
}

QByteArray textOf(AseBuffer *buffer) {
    size_t length = ase_buffer_length(buffer);
    QByteArray out(static_cast<int>(length), Qt::Uninitialized);
    if (length > 0) {
        ase_buffer_get_text(buffer, 0, length, out.data());
    }
    return out;
}

/* The Vim layer reads event->text(); a key sent without one is not the
 * key you meant. */
void press(EditorViewport &viewport, int key, const QString &text = QString()) {
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(&viewport, &event);
}

TextEdit edit(int line, int column, int length, const char *replacement) {
    return TextEdit{line, column, length, QByteArray(replacement)};
}

} // namespace

class MultiFileEdit : public QObject {
    Q_OBJECT

private slots:
    void oneEditLands() {
        AseBuffer *buffer = bufferFrom("int foo = 1;\n");
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.applyLineEdits({edit(1, 5, 3, "bar")}), 1);
        QCOMPARE(textOf(buffer), QByteArray("int bar = 1;\n"));
    }

    /* Two edits on one line: applying the first shifts the second, which
     * is the bug this whole path exists to avoid. */
    void twoEditsOnTheSameLine() {
        AseBuffer *buffer = bufferFrom("foo and foo\n");
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.applyLineEdits({edit(1, 1, 3, "x"), edit(1, 9, 3, "y")}), 2);
        QCOMPARE(textOf(buffer), QByteArray("x and y\n"));
    }

    /* A replacement longer than what it replaces shifts everything after
     * it further, in the other direction. */
    void aLongerReplacementStillLeavesTheRestCorrect() {
        AseBuffer *buffer = bufferFrom("a b a b\n");
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.applyLineEdits({edit(1, 1, 1, "LONG"), edit(1, 5, 1, "ALSOLONG")}), 2);
        QCOMPARE(textOf(buffer), QByteArray("LONG b ALSOLONG b\n"));
    }

    void editsAcrossLines() {
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.applyLineEdits({edit(1, 1, 3, "1"), edit(3, 1, 5, "3")}), 2);
        QCOMPARE(textOf(buffer), QByteArray("1\ntwo\n3\n"));
    }

    /* Given out of order, because a producer has no reason to sort. */
    void unsortedEditsStillLand() {
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.applyLineEdits({edit(3, 1, 5, "3"), edit(1, 1, 3, "1")}), 2);
        QCOMPARE(textOf(buffer), QByteArray("1\ntwo\n3\n"));
    }

    /* One group, so one `u` takes the whole file's share back — the
     * undo shape the design settled on. */
    void undoTakesBackEveryEditAtOnce() {
        AseBuffer *buffer = bufferFrom("foo and foo\n");
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.applyLineEdits({edit(1, 1, 3, "x"), edit(1, 9, 3, "y")}), 2);
        QCOMPARE(textOf(buffer), QByteArray("x and y\n"));

        press(viewport, Qt::Key_U, QStringLiteral("u"));
        QCOMPARE(textOf(buffer), QByteArray("foo and foo\n"));
    }

    void theBufferIsDirtyAfterwards() {
        AseBuffer *buffer = bufferFrom("foo\n");
        EditorViewport viewport(buffer, QString());
        QVERIFY(!viewport.isDirty());
        QCOMPARE(viewport.applyLineEdits({edit(1, 1, 3, "bar")}), 1);
        QVERIFY(viewport.isDirty());
    }

    /* A hit computed against a file that has changed since must not
     * corrupt what is there now. */
    void anOutOfRangeEditIsSkipped() {
        AseBuffer *buffer = bufferFrom("short\n");
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.applyLineEdits({edit(99, 1, 3, "x")}), 0);
        QCOMPARE(viewport.applyLineEdits({edit(1, 50, 3, "x")}), 0);
        QCOMPARE(viewport.applyLineEdits({edit(1, 1, 500, "x")}), 0);
        QCOMPARE(textOf(buffer), QByteArray("short\n"));
    }

    /* ---- grouping hits into per-file shares ---- */

    void acceptedHitsGroupByFile() {
        QVector<project::Replacement> items = {
            {{QStringLiteral("a.c"), 3, 5, QStringLiteral("x")}, true},
            {{QStringLiteral("b.c"), 1, 1, QStringLiteral("y")}, true},
            {{QStringLiteral("a.c"), 9, 2, QStringLiteral("z")}, true},
        };
        auto byFile = project::editsByFile(items, 3, QByteArray("NEW"));
        QCOMPARE(byFile.size(), 2);
        QCOMPARE(byFile[QStringLiteral("a.c")].size(), 2);
        QCOMPARE(byFile[QStringLiteral("b.c")].size(), 1);
        QCOMPARE(byFile[QStringLiteral("a.c")][0].line, 3);
        QCOMPARE(byFile[QStringLiteral("a.c")][0].column, 5);
        QCOMPARE(byFile[QStringLiteral("a.c")][0].length, 3);
        QCOMPARE(byFile[QStringLiteral("a.c")][0].replacement, QByteArray("NEW"));
    }

    /* A rejected hit must not reach the buffer — the preview is the
     * whole safety mechanism here, so this is the assertion that makes
     * it one. */
    void rejectedHitsAreLeftOut() {
        QVector<project::Replacement> items = {
            {{QStringLiteral("a.c"), 1, 1, QStringLiteral("x")}, true},
            {{QStringLiteral("a.c"), 2, 1, QStringLiteral("y")}, false},
            {{QStringLiteral("b.c"), 1, 1, QStringLiteral("z")}, false},
        };
        auto byFile = project::editsByFile(items, 3, QByteArray("NEW"));
        QCOMPARE(byFile.size(), 1);
        QCOMPARE(byFile[QStringLiteral("a.c")].size(), 1);
        QCOMPARE(byFile[QStringLiteral("a.c")][0].line, 1);
        QVERIFY(!byFile.contains(QStringLiteral("b.c")));

        QCOMPARE(project::acceptedCount(items), 1);
        QCOMPARE(project::acceptedFileCount(items), 1);
    }

    void rejectingEverythingGroupsNothing() {
        QVector<project::Replacement> items = {
            {{QStringLiteral("a.c"), 1, 1, QStringLiteral("x")}, false},
        };
        QVERIFY(project::editsByFile(items, 3, QByteArray("NEW")).isEmpty());
        QCOMPARE(project::acceptedCount(items), 0);
        QCOMPARE(project::acceptedFileCount(items), 0);
    }

    /* Deleting every occurrence is a replacement with nothing in it,
     * and is not the same as having nothing to do. */
    void anEmptyReplacementIsStillAnEdit() {
        QVector<project::Replacement> items = {
            {{QStringLiteral("a.c"), 1, 1, QStringLiteral("x")}, true},
        };
        auto byFile = project::editsByFile(items, 3, QByteArray());
        QCOMPARE(byFile[QStringLiteral("a.c")].size(), 1);
        QVERIFY(byFile[QStringLiteral("a.c")][0].replacement.isEmpty());
    }

    /* ---- end to end, one buffer ---- */

    void hitsFromASearchApplyToTheBufferTheyCameFrom() {
        const QByteArray text = "foo bar\nbaz foo\n";
        AseBuffer *buffer = bufferFrom(text);
        EditorViewport viewport(buffer, QString());

        QVector<project::Replacement> items = {
            {{QStringLiteral("x.c"), 1, 1, QStringLiteral("foo bar")}, true},
            {{QStringLiteral("x.c"), 2, 5, QStringLiteral("baz foo")}, true},
        };
        auto byFile = project::editsByFile(items, 3, QByteArray("qux"));
        QCOMPARE(viewport.applyLineEdits(byFile[QStringLiteral("x.c")]), 2);
        QCOMPARE(textOf(buffer), QByteArray("qux bar\nbaz qux\n"));
    }

    /* Nothing to do must leave no undo step for `u` to spend itself on. */
    void anEmptySetChangesNothing() {
        AseBuffer *buffer = bufferFrom("foo\n");
        EditorViewport viewport(buffer, QString());
        press(viewport, Qt::Key_I, QStringLiteral("i"));
        press(viewport, Qt::Key_X, QStringLiteral("x"));
        press(viewport, Qt::Key_Escape);
        QCOMPARE(textOf(buffer), QByteArray("xfoo\n"));

        QCOMPARE(viewport.applyLineEdits({}), 0);

        press(viewport, Qt::Key_U, QStringLiteral("u"));
        QCOMPARE(textOf(buffer), QByteArray("foo\n"));
    }
};

QTEST_MAIN(MultiFileEdit)
#include "test_multifile_edit.moc"
