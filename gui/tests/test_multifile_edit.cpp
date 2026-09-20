/*
 * Applying a set of edits to one buffer, which is the half of a
 * multi-file edit that can go wrong quietly: every edit shifts the
 * offsets of the ones after it. See docs/adr/0131.
 */
#include "editor_viewport.h"
#include "project_edit.h"
#include "lsp_workspace_edit.h"
#include "project_search.h"

#include "ase/buffer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
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
    return TextEdit{line, column, length, QByteArray(replacement), QByteArray()};
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
            {{QStringLiteral("a.c"), 3, 5, QStringLiteral("x")}, 3, QByteArray("NEW"), {}, true},
            {{QStringLiteral("b.c"), 1, 1, QStringLiteral("y")}, 3, QByteArray("NEW"), {}, true},
            {{QStringLiteral("a.c"), 9, 2, QStringLiteral("z")}, 3, QByteArray("NEW"), {}, true},
        };
        auto byFile = project::editsByFile(items);
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
            {{QStringLiteral("a.c"), 1, 1, QStringLiteral("x")}, 3, QByteArray("NEW"), {}, true},
            {{QStringLiteral("a.c"), 2, 1, QStringLiteral("y")}, 3, QByteArray("NEW"), {}, false},
            {{QStringLiteral("b.c"), 1, 1, QStringLiteral("z")}, 3, QByteArray("NEW"), {}, false},
        };
        auto byFile = project::editsByFile(items);
        QCOMPARE(byFile.size(), 1);
        QCOMPARE(byFile[QStringLiteral("a.c")].size(), 1);
        QCOMPARE(byFile[QStringLiteral("a.c")][0].line, 1);
        QVERIFY(!byFile.contains(QStringLiteral("b.c")));

        QCOMPARE(project::acceptedCount(items), 1);
        QCOMPARE(project::acceptedFileCount(items), 1);
    }

    void rejectingEverythingGroupsNothing() {
        QVector<project::Replacement> items = {
            {{QStringLiteral("a.c"), 1, 1, QStringLiteral("x")}, 3, QByteArray("NEW"), {}, false},
        };
        QVERIFY(project::editsByFile(items).isEmpty());
        QCOMPARE(project::acceptedCount(items), 0);
        QCOMPARE(project::acceptedFileCount(items), 0);
    }

    /* Deleting every occurrence is a replacement with nothing in it,
     * and is not the same as having nothing to do. */
    void anEmptyReplacementIsStillAnEdit() {
        QVector<project::Replacement> items = {
            {{QStringLiteral("a.c"), 1, 1, QStringLiteral("x")}, 3, QByteArray("NEW"), {}, true},
        };
        items[0].replacement = QByteArray();
        auto byFile = project::editsByFile(items);
        QCOMPARE(byFile[QStringLiteral("a.c")].size(), 1);
        QVERIFY(byFile[QStringLiteral("a.c")][0].replacement.isEmpty());
    }

    /* ---- `expected`: the guard that makes an edit checkable ---- */

    /* The whole point: a file that changed between the search and the
     * apply must not be edited at the offsets the search found. */
    void anEditWhoseBytesMovedIsSkipped() {
        AseBuffer *buffer = bufferFrom("int widget_total;\n");
        EditorViewport viewport(buffer, QString());
        TextEdit stale{1, 5, 6, QByteArray("gadget"), QByteArray("WIDGET")};
        QCOMPARE(viewport.applyLineEdits({stale}), 0);
        QCOMPARE(textOf(buffer), QByteArray("int widget_total;\n"));
    }

    void anEditWhoseBytesMatchIsApplied() {
        AseBuffer *buffer = bufferFrom("int widget_total;\n");
        EditorViewport viewport(buffer, QString());
        TextEdit good{1, 5, 6, QByteArray("gadget"), QByteArray("widget")};
        QCOMPARE(viewport.applyLineEdits({good}), 1);
        QCOMPARE(textOf(buffer), QByteArray("int gadget_total;\n"));
    }

    /* One bad edit in a set must not take the good ones with it, and
     * must not leave the good ones shifted by the bad one's length. */
    void aStaleEditDoesNotDisturbItsNeighbours() {
        AseBuffer *buffer = bufferFrom("aaa bbb ccc\n");
        EditorViewport viewport(buffer, QString());
        QVector<TextEdit> edits = {
            {1, 1, 3, QByteArray("XXX"), QByteArray("aaa")},
            {1, 5, 3, QByteArray("YYY"), QByteArray("zzz")}, /* not what is there */
            {1, 9, 3, QByteArray("ZZZ"), QByteArray("ccc")},
        };
        QCOMPARE(viewport.applyLineEdits(edits), 2);
        QCOMPARE(textOf(buffer), QByteArray("XXX bbb ZZZ\n"));
    }

    /* An empty `expected` means the producer did not know, which has to
     * keep working — it is how every edit arrived before the field. */
    void anEditWithNoExpectationIsTrusted() {
        AseBuffer *buffer = bufferFrom("anything\n");
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.applyLineEdits({edit(1, 1, 3, "XYZ")}), 1);
        QCOMPARE(textOf(buffer), QByteArray("XYZthing\n"));
    }

    /* ---- what the search has to hand over ---- */

    /* `text` is trimmed but `column` indexes the untrimmed line, so a
     * preview drawn with `column` landed one indent late — four
     * characters, in the first file this was ever run on. */
    void textColumnIndexesTheTrimmedLine() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(QDir(dir.path()).filePath(QStringLiteral("a.c")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("    return widget_total;\n");
        f.close();

        auto result = project::search(dir.path(), {QStringLiteral("a.c")}, QByteArray("widget"), 50);
        QCOMPARE(result.hits.size(), 1);
        const project::SearchHit &hit = result.hits[0];
        QCOMPARE(hit.column, 12);     /* on disk, past four spaces */
        QCOMPARE(hit.textColumn, 8);  /* in the trimmed text */
        QCOMPARE(hit.text.mid(hit.textColumn - 1, 6), QStringLiteral("widget"));
    }

    /* A results list wants one row per line. A replace wants all of
     * them, because the ones it silently skipped is the worst thing it
     * could do. */
    void everyOccurrenceIsOptedIntoSeparately() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(QDir(dir.path()).filePath(QStringLiteral("a.c")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("void widget_reset(void) { widget_total = 0; }\n");
        f.close();

        auto forSearch = project::search(dir.path(), {QStringLiteral("a.c")}, QByteArray("widget"), 50);
        QCOMPARE(forSearch.hits.size(), 1);

        auto forReplace =
            project::search(dir.path(), {QStringLiteral("a.c")}, QByteArray("widget"), 50, true);
        QCOMPARE(forReplace.hits.size(), 2);
        QVERIFY(forReplace.hits[0].column < forReplace.hits[1].column);
    }

    /* Both occurrences on one line, applied together, is the case the
     * reverse-order pass exists for. */
    void bothOccurrencesOnALineAreReplaced() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray line = "void widget_reset(void) { widget_total = 0; }\n";
        QFile f(QDir(dir.path()).filePath(QStringLiteral("a.c")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(line);
        f.close();

        auto result =
            project::search(dir.path(), {QStringLiteral("a.c")}, QByteArray("widget"), 50, true);
        QCOMPARE(result.hits.size(), 2);

        QVector<project::Replacement> items;
        for (const project::SearchHit &hit : result.hits) {
            items.push_back({hit, 6, QByteArray("gadget"), {}, true});
        }
        auto byFile = project::editsByFile(items);

        AseBuffer *buffer = bufferFrom(line);
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.applyLineEdits(byFile[QStringLiteral("a.c")]), 2);
        QCOMPARE(textOf(buffer), QByteArray("void gadget_reset(void) { gadget_total = 0; }\n"));
    }

    /* ---- reading a server's WorkspaceEdit ---- */

    void changesShapeIsRead() {
        const char *json = "{\"changes\":{ \"file:///p/a.c\":[{\"range\":{\"start\":{\"line\":2,\"character\":4}, \"end\":{\"line\":2,\"character\":10}},\"newText\":\"gadget\"}, {\"range\":{\"start\":{\"line\":7,\"character\":0}, \"end\":{\"line\":7,\"character\":6}},\"newText\":\"gadget\"}], \"file:///p/b.c\":[{\"range\":{\"start\":{\"line\":0,\"character\":1}, \"end\":{\"line\":0,\"character\":7}},\"newText\":\"gadget\"}]}}";
        AseJsonValue *v = ase_json_parse(json, strlen(json));
        QVERIFY(v != nullptr);
        auto items = lsp::replacementsFrom(v, QDir(QStringLiteral("/p")), QByteArray("widget"));
        QCOMPARE(items.size(), 3);
        /* 0-based on the wire, 1-based here. */
        QCOMPARE(items[0].hit.line, 3);
        QCOMPARE(items[0].hit.column, 5);
        QCOMPARE(items[0].length, 6);
        QCOMPARE(items[0].replacement, QByteArray("gadget"));
        QCOMPARE(items[0].expected, QByteArray("widget"));
        QCOMPARE(items[0].hit.path, QStringLiteral("a.c"));
        ase_json_destroy(v);
    }

    /* The other shape servers may answer in. Reading only one of them
     * works until a server that picks the other is used. */
    void documentChangesShapeIsRead() {
        const char *json = "{\"documentChanges\":[ {\"textDocument\":{\"uri\":\"file:///p/a.c\",\"version\":3}, \"edits\":[{\"range\":{\"start\":{\"line\":1,\"character\":2}, \"end\":{\"line\":1,\"character\":8}},\"newText\":\"gadget\"}]}]}";
        AseJsonValue *v = ase_json_parse(json, strlen(json));
        QVERIFY(v != nullptr);
        auto items = lsp::replacementsFrom(v, QDir(QStringLiteral("/p")), QByteArray("widget"));
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].hit.line, 2);
        QCOMPARE(items[0].hit.path, QStringLiteral("a.c"));
        ase_json_destroy(v);
    }

    /* A range this cannot represent must be dropped, not approximated:
     * applying a guess at a multi-line range eats code. */
    void aMultiLineRangeIsRefused() {
        const char *json = "{\"changes\":{\"file:///p/a.c\":[ {\"range\":{\"start\":{\"line\":1,\"character\":0}, \"end\":{\"line\":4,\"character\":6}},\"newText\":\"gadget\"}, {\"range\":{\"start\":{\"line\":6,\"character\":0}, \"end\":{\"line\":6,\"character\":6}},\"newText\":\"gadget\"}]}}";
        AseJsonValue *v = ase_json_parse(json, strlen(json));
        QVERIFY(v != nullptr);
        auto items = lsp::replacementsFrom(v, QDir(QStringLiteral("/p")), QByteArray("widget"));
        /* The good one survives; the impossible one does not. */
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].hit.line, 7);
        ase_json_destroy(v);
    }

    void malformedEntriesAreSkipped() {
        const char *json = "{\"changes\":{\"file:///p/a.c\":[ {\"newText\":\"gadget\"}, {\"range\":{\"start\":{\"line\":1,\"character\":5}, \"end\":{\"line\":1,\"character\":5}},\"newText\":\"gadget\"}, {\"range\":{\"start\":{\"line\":2,\"character\":0}, \"end\":{\"line\":2,\"character\":6}}}, {\"range\":{\"start\":{\"line\":3,\"character\":0}, \"end\":{\"line\":3,\"character\":6}},\"newText\":\"ok\"}]}}";
        AseJsonValue *v = ase_json_parse(json, strlen(json));
        QVERIFY(v != nullptr);
        auto items = lsp::replacementsFrom(v, QDir(QStringLiteral("/p")), QByteArray("widget"));
        /* No range, an empty range, and no newText all drop out. */
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].replacement, QByteArray("ok"));
        ase_json_destroy(v);
    }

    void aNonFileUriIsSkipped() {
        const char *json = "{\"changes\":{\"jar:///inside.class\":[ {\"range\":{\"start\":{\"line\":0,\"character\":0}, \"end\":{\"line\":0,\"character\":6}},\"newText\":\"gadget\"}]}}";
        AseJsonValue *v = ase_json_parse(json, strlen(json));
        QVERIFY(v != nullptr);
        QVERIFY(lsp::replacementsFrom(v, QDir(QStringLiteral("/p")), QByteArray("widget")).isEmpty());
        ase_json_destroy(v);
    }

    void anEmptyOrWrongResultIsEmpty() {
        QVERIFY(lsp::replacementsFrom(nullptr, QDir(QStringLiteral("/p")), QByteArray()).isEmpty());
        const char *json = "[]";
        AseJsonValue *v = ase_json_parse(json, strlen(json));
        QVERIFY(lsp::replacementsFrom(v, QDir(QStringLiteral("/p")), QByteArray()).isEmpty());
        ase_json_destroy(v);
    }

    /* ---- end to end, one buffer ---- */

    void hitsFromASearchApplyToTheBufferTheyCameFrom() {
        const QByteArray text = "foo bar\nbaz foo\n";
        AseBuffer *buffer = bufferFrom(text);
        EditorViewport viewport(buffer, QString());

        QVector<project::Replacement> items = {
            {{QStringLiteral("x.c"), 1, 1, QStringLiteral("foo bar")}, 3, QByteArray("qux"), {}, true},
            {{QStringLiteral("x.c"), 2, 5, QStringLiteral("baz foo")}, 3, QByteArray("qux"), {}, true},
        };
        auto byFile = project::editsByFile(items);
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
