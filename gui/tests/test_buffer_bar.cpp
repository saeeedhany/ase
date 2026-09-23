/*
 * The tab strip's name elision — see docs/adr/0138.
 *
 * Only the part that can be asserted on: how a name is shortened. What
 * the strip actually draws is checked by looking at it.
 */
#include "buffer_bar.h"

#include <QFont>
#include <QFontMetrics>
#include <QTest>

class BufferBarNames : public QObject {
    Q_OBJECT

private slots:

    void aShortNameIsLeftAlone() {
        QFontMetrics metrics(QFont(QStringLiteral("monospace"), 11));
        const QString name = QStringLiteral("main.cpp");
        QCOMPARE(bufferbar::elideTabName(metrics, name,
                                          bufferbar::maxTabNameWidth(metrics)),
                 name);
    }

    /* One pathological filename must not be able to push the rest of the
     * strip off the screen. */
    void aLongNameIsShortenedToFit() {
        QFontMetrics metrics(QFont(QStringLiteral("monospace"), 11));
        const int maxWidth = bufferbar::maxTabNameWidth(metrics);
        const QString name =
            QStringLiteral("AbsolutelyEnormousFileNameThatNobodyWouldReallyWrite.cpp");
        const QString elided = bufferbar::elideTabName(metrics, name, maxWidth);
        QVERIFY(elided != name);
        QVERIFY(metrics.horizontalAdvance(elided) <= maxWidth);
    }

    /* Both ends survive, because the start says which file and the end
     * says what kind. Two names differing only in the tail must stay
     * distinguishable. */
    void elisionKeepsBothEnds() {
        QFontMetrics metrics(QFont(QStringLiteral("monospace"), 11));
        const int maxWidth = bufferbar::maxTabNameWidth(metrics);
        const QString a = bufferbar::elideTabName(
            metrics, QStringLiteral("EditorViewportRenderer.cpp"), maxWidth);
        const QString b = bufferbar::elideTabName(
            metrics, QStringLiteral("EditorViewportRenderer.h"), maxWidth);
        QVERIFY(a != b);
        QVERIFY(a.startsWith(QStringLiteral("Editor")));
        QVERIFY(a.endsWith(QStringLiteral(".cpp")));
        QVERIFY(b.endsWith(QStringLiteral(".h")));
    }

    /* A limit that makes no sense is not a reason to return nothing. */
    void aZeroWidthLimitLeavesTheNameAlone() {
        QFontMetrics metrics(QFont(QStringLiteral("monospace"), 11));
        const QString name = QStringLiteral("whatever.cpp");
        QCOMPARE(bufferbar::elideTabName(metrics, name, 0), name);
    }
};

QTEST_MAIN(BufferBarNames)
#include "test_buffer_bar.moc"
