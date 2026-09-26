/*
 * What a screen reader is told — see docs/adr/0146.
 *
 * Through QAccessible::queryAccessibleInterface, which is the path an
 * assistive technology takes, not by calling the class directly. What
 * this cannot check is how Orca or NVDA then reads it aloud; what it
 * can check is that every answer is the right one.
 */
#include "editor_viewport.h"

#include "ase/buffer.h"

#include <QAccessible>
#include <QApplication>
#include <QKeyEvent>
#include <QTest>

namespace {

/* What Qt would hand to an assistive technology. Captured through the
 * public update handler, which is the same seam a platform plugin
 * uses. */
struct Announcement {
    QAccessible::Event type = QAccessible::InvalidEvent;
    int position = -1;
    QString inserted;
    QString removed;
    int cursor = -1;
};

QList<Announcement> g_heard;

void hear(QAccessibleEvent *event) {
    Announcement a;
    a.type = event->type();
    if (auto *cursor = dynamic_cast<QAccessibleTextCursorEvent *>(event)) {
        a.cursor = cursor->cursorPosition();
    }
    if (auto *inserted = dynamic_cast<QAccessibleTextInsertEvent *>(event)) {
        a.position = inserted->changePosition();
        a.inserted = inserted->textInserted();
    }
    if (auto *removed = dynamic_cast<QAccessibleTextRemoveEvent *>(event)) {
        a.position = removed->changePosition();
        a.removed = removed->textRemoved();
    }
    if (auto *updated = dynamic_cast<QAccessibleTextUpdateEvent *>(event)) {
        a.position = updated->changePosition();
        a.inserted = updated->textInserted();
        a.removed = updated->textRemoved();
    }
    g_heard.append(a);
}

Announcement lastOfType(QAccessible::Event type) {
    for (int i = g_heard.size() - 1; i >= 0; --i) {
        if (g_heard.at(i).type == type) {
            return g_heard.at(i);
        }
    }
    return {};
}

void type(EditorViewport &viewport, const QString &text) {
    for (const QChar &c : text) {
        QKeyEvent press(QEvent::KeyPress, c.unicode(), Qt::NoModifier, QString(c));
        QApplication::sendEvent(&viewport, &press);
    }
}

AseBuffer *bufferFrom(const QByteArray &text) {
    AseBuffer *buffer = ase_buffer_create();
    if (!text.isEmpty()) {
        ase_buffer_insert(buffer, 0, text.constData(), static_cast<size_t>(text.size()));
    }
    return buffer;
}

} // namespace

class Accessible : public QObject {
    Q_OBJECT

private:
    static QAccessibleTextInterface *textOf(EditorViewport &viewport) {
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&viewport);
        return iface != nullptr ? iface->textInterface() : nullptr;
    }

private slots:
    void initTestCase() {
        QAccessible::installUpdateHandler(hear);
        /* Deliberately NOT QAccessible::setActive(true): that writes
         * through to the platform accessibility plugin, and a CI runner
         * has none, so isActive() stays false however often it is
         * called. Running with it false here is what makes this test
         * mean the same thing on both machines. */
        EditorViewport::setAccessibilityAlwaysOn(true);
    }

    void init() { g_heard.clear(); }

    /* ---- what a reader is told, not just what it can ask ---- */

    /*
     * The decision, separately from Qt delivering it: which edit gets
     * announced. Qt 6.2 drops an accessibility event when no platform
     * plugin reports active and Qt 6.11 does not, so the tests below
     * that watch for the event itself cannot assert on every machine —
     * these can.
     */
    void theSmallestEditIsWhatGetsAnnounced() {
        int at = -1;
        QString removed;
        QString inserted;

        /* One character typed in the middle. */
        EditorViewport::a11yEditBetween(QStringLiteral("hello world"),
                                         QStringLiteral("hello! world"), &at, &removed, &inserted);
        QCOMPARE(at, 5);
        QCOMPARE(inserted, QStringLiteral("!"));
        QVERIFY(removed.isEmpty());

        /* One character deleted. */
        EditorViewport::a11yEditBetween(QStringLiteral("hello"), QStringLiteral("hllo"), &at,
                                         &removed, &inserted);
        QCOMPARE(at, 1);
        QCOMPARE(removed, QStringLiteral("e"));
        QVERIFY(inserted.isEmpty());

        /* A replacement is both at once, which is a third kind of event. */
        EditorViewport::a11yEditBetween(QStringLiteral("cat"), QStringLiteral("cow"), &at, &removed,
                                         &inserted);
        QCOMPARE(at, 1);
        QCOMPARE(removed, QStringLiteral("at"));
        QCOMPARE(inserted, QStringLiteral("ow"));

        /* At the very start and the very end. */
        EditorViewport::a11yEditBetween(QStringLiteral("bc"), QStringLiteral("abc"), &at, &removed,
                                         &inserted);
        QCOMPARE(at, 0);
        QCOMPARE(inserted, QStringLiteral("a"));
        EditorViewport::a11yEditBetween(QStringLiteral("ab"), QStringLiteral("abc"), &at, &removed,
                                         &inserted);
        QCOMPARE(at, 2);
        QCOMPARE(inserted, QStringLiteral("c"));

        /* Nothing changed is nothing to say. */
        EditorViewport::a11yEditBetween(QStringLiteral("same"), QStringLiteral("same"), &at,
                                         &removed, &inserted);
        QVERIFY(removed.isEmpty());
        QVERIFY(inserted.isEmpty());
    }

    /* Repeated characters are where a naive prefix/suffix walk goes
     * wrong: the two halves must not claim the same characters. */
    void anEditInsideARunOfTheSameCharacter() {
        int at = -1;
        QString removed;
        QString inserted;
        EditorViewport::a11yEditBetween(QStringLiteral("aaaa"), QStringLiteral("aaa"), &at, &removed,
                                         &inserted);
        QCOMPARE(at, 3);
        QCOMPARE(removed, QStringLiteral("a"));
        QVERIFY(inserted.isEmpty());
    }

    /* Positions are character indices here too, not bytes. */
    void anEditIsPositionedInCharacters() {
        int at = -1;
        QString removed;
        QString inserted;
        EditorViewport::a11yEditBetween(QString::fromUtf8("héllo"), QString::fromUtf8("héllo!"),
                                         &at, &removed, &inserted);
        QCOMPARE(at, 5);
        QCOMPARE(inserted, QStringLiteral("!"));
    }

    /* A reader that is only queried reads a stale document. */
    void typingIsAnnouncedAsAnInsert() {
        AseBuffer *buffer = bufferFrom("hello world\n");
        EditorViewport viewport(buffer, QString());
        viewport.restorePosition(5, 0);
        g_heard.clear();

        type(viewport, QStringLiteral("i!"));

        if (g_heard.isEmpty()) {
            QSKIP("this Qt drops accessibility events while no platform plugin reports active");
        }
        const Announcement said = lastOfType(QAccessible::TextInserted);
        QCOMPARE(said.inserted, QStringLiteral("!"));
        QCOMPARE(said.position, 5);
    }

    void deletingIsAnnouncedAsARemove() {
        AseBuffer *buffer = bufferFrom("hello\n");
        EditorViewport viewport(buffer, QString());
        viewport.restorePosition(0, 0);
        g_heard.clear();

        type(viewport, QStringLiteral("x"));

        if (g_heard.isEmpty()) {
            QSKIP("this Qt drops accessibility events while no platform plugin reports active");
        }
        const Announcement said = lastOfType(QAccessible::TextRemoved);
        QCOMPARE(said.removed, QStringLiteral("h"));
        QCOMPARE(said.position, 0);
    }

    void movingTheCaretIsAnnounced() {
        AseBuffer *buffer = bufferFrom("hello\nworld\n");
        EditorViewport viewport(buffer, QString());
        viewport.restorePosition(0, 0);
        g_heard.clear();

        type(viewport, QStringLiteral("j"));

        if (g_heard.isEmpty()) {
            QSKIP("this Qt drops accessibility events while no platform plugin reports active");
        }
        const Announcement said = lastOfType(QAccessible::TextCaretMoved);
        QCOMPARE(said.cursor, 6);
    }

    /* Nothing is computed when nothing is listening — the diff walks
     * the whole buffer, so it must not run on every keystroke for
     * everyone else. Passing here is also what proves the tests above
     * ran with isActive() false, which is the condition on a machine
     * with no accessibility plugin. */
    void nothingIsAnnouncedWhenNoReaderIsAttached() {
        EditorViewport::setAccessibilityAlwaysOn(false);
        AseBuffer *buffer = bufferFrom("hello\n");
        EditorViewport viewport(buffer, QString());
        g_heard.clear();
        type(viewport, QStringLiteral("ix"));
        QCOMPARE(g_heard.size(), 0);
        EditorViewport::setAccessibilityAlwaysOn(true);
    }

    /* Without a factory the viewport is an opaque rectangle: a reader
     * gets a name and no contents at all. */
    void theEditorIsReachableAndEditable() {
        AseBuffer *buffer = bufferFrom("hello\nworld\n");
        EditorViewport viewport(buffer, QStringLiteral("/tmp/notes.txt"));

        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&viewport);
        QVERIFY(iface != nullptr);
        QCOMPARE(iface->role(), QAccessible::EditableText);
        QVERIFY(iface->state().editable);
        QVERIFY(iface->state().multiLine);
        QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("notes.txt"));
        QCOMPARE(iface->text(QAccessible::Value), QStringLiteral("hello\nworld\n"));
        QVERIFY(iface->textInterface() != nullptr);
    }

    void anUnnamedBufferSaysSo() {
        EditorViewport viewport(bufferFrom(""), QString());
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&viewport);
        QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("Untitled"));
    }

    void theCaretIsReportedAndCanBeMoved() {
        AseBuffer *buffer = bufferFrom("hello\nworld\n");
        EditorViewport viewport(buffer, QString());
        viewport.restorePosition(3, 0);

        QAccessibleTextInterface *text = textOf(viewport);
        QVERIFY(text != nullptr);
        QCOMPARE(text->characterCount(), 12);
        QCOMPARE(text->cursorPosition(), 3);

        text->setCursorPosition(8);
        QCOMPARE(text->cursorPosition(), 8);
        QCOMPARE(viewport.cursorOffset(), size_t(8));
    }

    void aSelectionIsReportedAndCanBeSet() {
        AseBuffer *buffer = bufferFrom("hello world\n");
        EditorViewport viewport(buffer, QString());
        QAccessibleTextInterface *text = textOf(viewport);

        QCOMPARE(text->selectionCount(), 0);

        text->setSelection(0, 6, 11);
        QCOMPARE(text->selectionCount(), 1);
        int start = -1;
        int end = -1;
        text->selection(0, &start, &end);
        QCOMPARE(start, 6);
        QCOMPARE(end, 11);
        QCOMPARE(text->text(start, end), QStringLiteral("world"));
    }

    /* The whole reason the offsets are converted: everything inside the
     * editor counts UTF-8 bytes, and a reader counts characters. They
     * agree only while a file is ASCII. */
    void offsetsAreCharactersNotBytes() {
        /* "héllo wörld" — two two-byte characters, so byte offsets run
         * ahead of character offsets by two at the end. */
        AseBuffer *buffer = bufferFrom(QString::fromUtf8("héllo wörld\n").toUtf8());
        EditorViewport viewport(buffer, QString());
        QAccessibleTextInterface *text = textOf(viewport);

        QCOMPARE(text->characterCount(), 12);
        QCOMPARE(text->text(0, 5), QString::fromUtf8("héllo"));

        /* Character 7 is the 'ö', which starts at byte 8. */
        text->setCursorPosition(7);
        QCOMPARE(text->cursorPosition(), 7);
        QCOMPARE(viewport.cursorOffset(), size_t(8));
        QCOMPARE(text->text(7, 8), QString::fromUtf8("ö"));
    }

    void aLineIsReadWithoutItsNewline() {
        AseBuffer *buffer = bufferFrom("first\nsecond\nthird\n");
        EditorViewport viewport(buffer, QString());
        QAccessibleTextInterface *text = textOf(viewport);

        int start = -1;
        int end = -1;
        QCOMPARE(text->textAtOffset(7, QAccessible::LineBoundary, &start, &end),
                 QStringLiteral("second"));
        QCOMPARE(start, 6);
        QCOMPARE(end, 12);

        QCOMPARE(text->textBeforeOffset(7, QAccessible::LineBoundary, &start, &end),
                 QStringLiteral("first"));
        QCOMPARE(text->textAfterOffset(7, QAccessible::LineBoundary, &start, &end),
                 QStringLiteral("third"));
    }

    /* -1 is how a reader asks "the line I am on". */
    void offsetMinusOneMeansTheCaret() {
        AseBuffer *buffer = bufferFrom("first\nsecond\nthird\n");
        EditorViewport viewport(buffer, QString());
        viewport.restorePosition(8, 0);
        QAccessibleTextInterface *text = textOf(viewport);

        int start = -1;
        int end = -1;
        QCOMPARE(text->textAtOffset(-1, QAccessible::LineBoundary, &start, &end),
                 QStringLiteral("second"));
    }

    void theEndsOfTheBufferHaveNothingBeyondThem() {
        AseBuffer *buffer = bufferFrom("only\n");
        EditorViewport viewport(buffer, QString());
        QAccessibleTextInterface *text = textOf(viewport);

        int start = 0;
        int end = 0;
        QCOMPARE(text->textBeforeOffset(0, QAccessible::LineBoundary, &start, &end), QString());
        QCOMPARE(start, -1);
        QCOMPARE(end, -1);
    }

    void wordsUseThePlatformsIdeaOfAWord() {
        AseBuffer *buffer = bufferFrom("alpha beta gamma\n");
        EditorViewport viewport(buffer, QString());
        QAccessibleTextInterface *text = textOf(viewport);

        int start = -1;
        int end = -1;
        QCOMPARE(text->textAtOffset(7, QAccessible::WordBoundary, &start, &end),
                 QStringLiteral("beta"));
        QCOMPARE(start, 6);
        QCOMPARE(end, 10);
    }

    void aCharacterHasAPlaceOnScreen() {
        AseBuffer *buffer = bufferFrom("abc\ndef\n");
        EditorViewport viewport(buffer, QString());
        viewport.resize(400, 200);
        QAccessibleTextInterface *text = textOf(viewport);

        const QRect first = text->characterRect(0);
        const QRect second = text->characterRect(1);
        const QRect nextLine = text->characterRect(4);

        QVERIFY(first.isValid());
        QVERIFY(second.left() > first.left());   /* along the line */
        QCOMPARE(second.top(), first.top());
        QVERIFY(nextLine.top() > first.top());   /* and down a row */
    }

    void aPointMapsBackToAnOffset() {
        AseBuffer *buffer = bufferFrom("abc\ndef\n");
        EditorViewport viewport(buffer, QString());
        viewport.resize(400, 200);
        QAccessibleTextInterface *text = textOf(viewport);

        const QRect rect = text->characterRect(5);
        QCOMPARE(text->offsetAtPoint(rect.center()), 5);
    }
};

QTEST_MAIN(Accessible)
#include "test_accessible.moc"
