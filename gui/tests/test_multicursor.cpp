/*
 * Several carets at once: adding them with `Ctrl+D` (docs/adr/0138) and
 * moving them vertically, each keeping its own column (docs/adr/0139).
 */
#include "editor_viewport.h"

#include "command_registry.h"

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

} // namespace

/* Through the command, not the private method: that is the path the key
 * actually takes. A bare viewport has no registry until the window gives
 * it one, so the test does what the window does. */

class MultiCursor : public QObject {
    Q_OBJECT

private:
    CommandRegistry m_windowCommands;

    bool addCaret(EditorViewport &viewport) {
        return viewport.runCommandByName(
            QStringLiteral("editor.cursor.add-next-occurrence"));
    }

private slots:
    void addsOneCaretPerPress() {
        AseBuffer *buffer = bufferFrom("foo bar foo baz foo\n");
        EditorViewport viewport(buffer, QString());
        viewport.registerCommands(&m_windowCommands);
        viewport.goToLineColumn(1, 2); /* inside the first foo */
        QCOMPARE(viewport.cursorCount(), 1);
        QVERIFY(addCaret(viewport));
        QCOMPARE(viewport.cursorCount(), 2);
        QVERIFY(addCaret(viewport));
        QCOMPARE(viewport.cursorCount(), 3);
    }

    /* The reason this exists: stopping at the end of the buffer made the
     * command depend on where in the file you started. */
    void wrapsPastTheEndOfTheBuffer() {
        AseBuffer *buffer = bufferFrom("foo bar foo baz foo\n");
        EditorViewport viewport(buffer, QString());
        viewport.registerCommands(&m_windowCommands);
        viewport.goToLineColumn(1, 18); /* inside the LAST foo */
        QVERIFY(addCaret(viewport));
        /* Nothing after it, so this has to come from the top. */
        QCOMPARE(viewport.cursorCount(), 2);
        QVERIFY(addCaret(viewport));
        QCOMPARE(viewport.cursorCount(), 3);
    }

    /* Once every occurrence has one, pressing again does nothing rather
     * than adding a duplicate that normalizeCursors() would drop. */
    void stopsWhenEveryOccurrenceIsTaken() {
        AseBuffer *buffer = bufferFrom("foo bar foo\n");
        EditorViewport viewport(buffer, QString());
        viewport.registerCommands(&m_windowCommands);
        viewport.goToLineColumn(1, 2);
        QVERIFY(addCaret(viewport));
        QCOMPARE(viewport.cursorCount(), 2);
        for (int i = 0; i < 5; ++i) {
            QVERIFY(addCaret(viewport));
        }
        QCOMPARE(viewport.cursorCount(), 2);
    }

    /* A substring is not an occurrence: `in` must not match inside
     * `int`. */
    void matchesWholeWordsOnly() {
        AseBuffer *buffer = bufferFrom("in int inside in\n");
        EditorViewport viewport(buffer, QString());
        viewport.registerCommands(&m_windowCommands);
        viewport.goToLineColumn(1, 1);
        QVERIFY(addCaret(viewport));
        QCOMPARE(viewport.cursorCount(), 2);
        /* Only the two bare `in`s, so the second press finds nothing. */
        QVERIFY(addCaret(viewport));
        QCOMPARE(viewport.cursorCount(), 2);
    }

    void aCaretTouchingNoWordDoesNothing() {
        AseBuffer *buffer = bufferFrom("   \n");
        EditorViewport viewport(buffer, QString());
        viewport.registerCommands(&m_windowCommands);
        viewport.goToLineColumn(1, 2);
        QVERIFY(addCaret(viewport));
        QCOMPARE(viewport.cursorCount(), 1);
    }

    /* A single occurrence has nowhere to wrap to. */
    void theOnlyOccurrenceAddsNothing() {
        AseBuffer *buffer = bufferFrom("alone here\n");
        EditorViewport viewport(buffer, QString());
        viewport.registerCommands(&m_windowCommands);
        viewport.goToLineColumn(1, 2);
        QVERIFY(addCaret(viewport));
        QCOMPARE(viewport.cursorCount(), 1);
    }

    /* Carets land after the word, in buffer order, however they were
     * collected. */
    void caretsAreOrderedAndAfterEachWord() {
        AseBuffer *buffer = bufferFrom("foo bar foo\n");
        EditorViewport viewport(buffer, QString());
        viewport.registerCommands(&m_windowCommands);
        viewport.goToLineColumn(1, 10); /* inside the second foo */
        QVERIFY(addCaret(viewport));
        const QVector<size_t> at = viewport.cursorOffsets();
        QCOMPARE(at.size(), 2);
        /* The added one sits after the word it found; the one already
         * there stays where it was put. */
        QCOMPARE(at[0], size_t(3)); /* end of the first "foo" */
        QCOMPARE(at[1], size_t(9)); /* untouched, inside the second */
    }


    /* ---- one sticky column per caret (ADR 0139) ---- */

    /*
     * The middle line is too short for the right-hand caret, which is
     * the whole point: it has to come back to column 12 on the way out,
     * not to the column the short line clipped it to.
     *
     * Short, but not so short that both carets land on the same offset
     * — two carets in one place are one caret, here as everywhere else.
     */
    void eachCaretKeepsItsOwnColumn() {
        AseBuffer *buffer = bufferFrom("ab xx efg xx z\nkkkkkkkk\nmnopqrstuvwxyz\n");
        EditorViewport viewport(buffer, QString());
        viewport.registerCommands(&m_windowCommands);
        viewport.restorePosition(4, 0);
        QVERIFY(addCaret(viewport));
        QCOMPARE(viewport.cursorOffsets(), (QVector<size_t>{4, 12}));

        QTest::keyClick(&viewport, Qt::Key_Down);
        QTest::keyClick(&viewport, Qt::Key_Down);

        QCOMPARE(viewport.cursorCount(), 2);
        QCOMPARE(viewport.cursorOffsets(), (QVector<size_t>{28, 36}));
    }

    void caretsComingBackUpAreWhereTheyStarted() {
        AseBuffer *buffer = bufferFrom("ab xx efg xx z\nkkkkkkkk\nmnopqrstuvwxyz\n");
        EditorViewport viewport(buffer, QString());
        viewport.registerCommands(&m_windowCommands);
        viewport.restorePosition(4, 0);
        QVERIFY(addCaret(viewport));

        QTest::keyClick(&viewport, Qt::Key_Down);
        QTest::keyClick(&viewport, Qt::Key_Up);

        QCOMPARE(viewport.cursorOffsets(), (QVector<size_t>{4, 12}));
    }

};

QTEST_MAIN(MultiCursor)
#include "test_multicursor.moc"
