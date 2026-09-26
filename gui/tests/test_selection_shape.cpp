/*
 * What a selection paints as — see docs/adr/0144.
 *
 * Not the pixels, the rectangles: how many, how wide, and whether a
 * line gets one at all. ADR 0123 fixed a sliver here by looking at it,
 * and two more edges stayed wrong underneath.
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

} // namespace

class SelectionShape : public QObject {
    Q_OBJECT

private slots:
    /* The whole buffer, linewise: one rect per line and none wider than
     * its own text. */
    void aLinewiseLineStopsAtItsLastCharacter() {
        AseBuffer *buffer = bufferFrom("short\nmuch longer line\n");
        EditorViewport viewport(buffer, QString());

        const QVector<QRectF> lines =
            viewport.highlightRects(0, 22, 0, 3, /*linewise=*/true);
        QCOMPARE(lines.size(), 2);

        const QVector<QRectF> chars =
            viewport.highlightRects(0, 22, 0, 3, /*linewise=*/false);
        QCOMPARE(chars.size(), 2);

        /* Charwise keeps the half-cell that says the line break is in
         * the selection; linewise does not, because every break is.
         * Widths are the measured advance of the text, not a count of
         * cells — m_charWidth is one glyph's advance and not every
         * glyph's. */
        QCOMPARE(chars.at(0).width() - lines.at(0).width(), qreal(viewport.charWidth() / 2));
        /* The last line of a range never gets the hint in either mode:
         * there is no following line for the break to lead to. */
        QCOMPARE(chars.at(1).width(), lines.at(1).width());
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(chars.at(i).left(), lines.at(i).left());
            QVERIFY(lines.at(i).width() > 0);
        }
        /* Linewise follows the text, so a short line gets a short rect. */
        QVERIFY(lines.at(0).width() < lines.at(1).width());
    }

    /* A blank line between two selected ones is selected too, and has
     * to look it. */
    void anEmptyLineInsideALinewiseSelectionIsPainted() {
        AseBuffer *buffer = bufferFrom("one\n\ntwo\n");
        EditorViewport viewport(buffer, QString());

        const QVector<QRectF> rects =
            viewport.highlightRects(0, 9, 0, 4, /*linewise=*/true);
        QCOMPARE(rects.size(), 3);
        QCOMPARE(rects.at(1).width(), qreal(viewport.charWidth()));
        /* Contiguous: three rows, each one line tall, no gap. */
        QCOMPARE(rects.at(1).top(), rects.at(0).bottom());
        QCOMPARE(rects.at(2).top(), rects.at(1).bottom());
    }

    /* Charwise must not grow one, or every blank line in a long
     * selection sprouts a block. */
    void anEmptyLineCharwiseGetsNothing() {
        AseBuffer *buffer = bufferFrom("one\n\ntwo\n");
        EditorViewport viewport(buffer, QString());

        const QVector<QRectF> rects =
            viewport.highlightRects(0, 9, 0, 4, /*linewise=*/false);
        QCOMPARE(rects.size(), 2);
    }

    /* The ADR 0123 case: a range ending exactly at a line's start must
     * not paint that line. */
    void aRangeEndingAtALineStartLeavesItAlone() {
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());

        const QVector<QRectF> rects =
            viewport.highlightRects(0, 4, 0, 4, /*linewise=*/false);
        QCOMPARE(rects.size(), 1);
    }

    void anEmptySelectionPaintsNothing() {
        AseBuffer *buffer = bufferFrom("one\ntwo\n");
        EditorViewport viewport(buffer, QString());
        QCOMPARE(viewport.highlightRects(4, 4, 0, 3, true).size(), 0);
        QCOMPARE(viewport.highlightRects(5, 2, 0, 3, false).size(), 0);
    }
};

QTEST_MAIN(SelectionShape)
#include "test_selection_shape.moc"
