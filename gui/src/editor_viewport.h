#ifndef ASE_EDITOR_VIEWPORT_H
#define ASE_EDITOR_VIEWPORT_H

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

extern "C" {
#include "ase/buffer.h"
#include "ase/config.h"
#include "ase/syntax.h"
}

class QTimer;
class QPainter;

/*
 * Custom-painted text viewport: fills the whole window, no chrome of its
 * own. Owns the AseBuffer it renders — see docs/adr/0002 for why this
 * class, not the core, holds Qt-specific state. Known v1 shortcuts (full-
 * buffer mirroring, byte-level cursor, no IME) are documented in
 * docs/adr/0006, not repeated here. Multi-cursor, the opt-in caret-fade
 * animation, and the accessibility pass are documented in docs/adr/0012.
 */
class EditorViewport : public QWidget {
public:
    EditorViewport(AseBuffer *buffer, QString filePath, QWidget *parent = nullptr);
    ~EditorViewport() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void loadConfig();
    void applyConfig();
    void checkConfigReload();
    void refreshCache();
    void drawLine(QPainter &painter, int start, int end, int y);
    QFont fontForCapture(AseHighlightCapture capture) const;
    QColor colorForCapture(AseHighlightCapture capture) const;
    QVector<AseHighlightCapture> capturesForLine(int start, int end) const;
    /* Exact pixel x of `column` within [lineStart, lineEnd), measured the
     * same way drawLine actually renders (per-run, with that run's real
     * font) rather than assumed via column * m_charWidth — see
     * docs/adr/0013's caret-drift fix. */
    int xForColumn(int lineStart, int lineEnd, int column) const;

    /* 0 when line numbers are off; otherwise measured (not assumed —
     * see docs/adr/0014) from the widest line-number string actually
     * needed. */
    int gutterWidth() const;
    QString gutterLabelForLine(int line, int cursorLine) const;

    /* Eases m_renderedScrollLine/X and m_renderedCaretPos toward their
     * logical targets (m_scrollLine/X, m_cursors) by one step. Called
     * from the top of paintEvent, not the timer, so it's never stale
     * relative to what's about to be drawn — see docs/adr/0015. When
     * animations are off, snaps rendered state to the target instead
     * of easing (today's instant behavior, preserved). */
    void updateAnimation();
    /* Where cursor's caret should render right now, in widget pixel
     * space, given the *current* (possibly still-easing) scroll
     * position — not its final settled position. */
    QPointF caretTargetFor(size_t cursor) const;
    /* Forces the caret solid-visible and restarts the idle countdown
     * to the next hard-blink toggle — call on every cursor-moving
     * action (key or mouse) so the caret never blinks away mid-use and
     * resumes a normal blink only once activity actually stops. See
     * docs/adr/0016. */
    void resetCaretBlink();

    /* All of these act on every cursor in m_cursors (a single cursor is
     * just the size-1 case) — see docs/adr/0012, decision 1, for why
     * processing highest-offset-first needs no cross-cursor bookkeeping.
     * Each loops over m_cursors calling the matching *At single-cursor
     * primitive below. */
    void insertText(const QByteArray &bytes);
    void deleteBackward();
    void deleteForward();
    void moveCursorLeft();
    void moveCursorRight();
    void moveCursorVertically(int lineDelta);
    void moveCursorHome();
    void moveCursorEnd();
    void addCursorAtNextOccurrence();
    void collapseToOneCursor();
    void normalizeCursors();

    /* Single-cursor primitives — operate on one cursor's position. */
    void insertTextAt(size_t &cursor, const QByteArray &bytes);
    void deleteBackwardAt(size_t &cursor);
    void deleteForwardAt(size_t &cursor);
    void moveCursorLeftAt(size_t &cursor);
    void moveCursorRightAt(size_t &cursor);
    void moveCursorVerticallyAt(size_t &cursor, int lineDelta);
    void moveCursorHomeAt(size_t &cursor);
    void moveCursorEndAt(size_t &cursor);

    void ensureCursorVisible();
    void save();

    size_t offsetForPoint(const QPoint &pos) const;
    int lineForOffset(size_t offset) const;
    int columnForOffset(size_t offset, int line) const;
    size_t offsetForLineColumn(int line, int column) const;

    AseBuffer *m_buffer;
    QString m_filePath;

    AseConfig *m_config = nullptr;
    QString m_configPath;
    QDateTime m_configModified;
    QTimer *m_configTimer;
    QColor m_backgroundColor;
    QColor m_textColor;
    bool m_animationsEnabled = false;

    AseSyntax *m_syntax = nullptr; /* null for unsupported file types — see docs/adr/0007 */
    QVector<AseHighlightSpan> m_highlights;

    QByteArray m_cache;
    QVector<int> m_lineStarts;

    QVector<size_t> m_cursors {0}; /* always non-empty, sorted ascending, de-duplicated */
    int m_scrollLine = 0;
    int m_scrollX = 0; /* leftmost visible pixel, not column — see docs/adr/0014 */
    int m_desiredColumn = -1; /* sticky column — single-cursor mode only, see docs/adr/0012 */
    QString m_lineNumberMode = QStringLiteral("absolute"); /* "off" / "absolute" / "relative" */

    /* Rendered (possibly still-easing) counterparts of m_scrollLine/X and
     * m_cursors — see docs/adr/0015. Equal to the logical values whenever
     * animations are off or nothing is moving. */
    double m_renderedScrollLine = 0.0;
    double m_renderedScrollX = 0.0;
    QVector<QPointF> m_renderedCaretPos;

    QFont m_font;
    int m_lineHeight = 0;
    int m_charWidth = 0;

    QTimer *m_blinkTimer;
    bool m_caretVisible = true;
    int m_caretTick = 0;   /* drives the animated fade phase only — see docs/adr/0016 */
    int m_idleTicks = 0;   /* ticks since the last cursor-moving action; drives the
                             * hard blink's toggle, kept separate from m_caretTick so
                             * activity doesn't restart the fade's phase too */
};

#endif /* ASE_EDITOR_VIEWPORT_H */
