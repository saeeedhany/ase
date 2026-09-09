#ifndef ASE_EDITOR_VIEWPORT_H
#define ASE_EDITOR_VIEWPORT_H

#include <QByteArray>
#include <QFont>
#include <QString>
#include <QVector>
#include <QWidget>

extern "C" {
#include "ase/buffer.h"
#include "ase/syntax.h"
}

class QTimer;
class QPainter;

/*
 * Custom-painted text viewport: fills the whole window, no chrome of its
 * own. Owns the AseBuffer it renders — see docs/adr/0002 for why this
 * class, not the core, holds Qt-specific state. Known v1 shortcuts (full-
 * buffer mirroring, byte-level cursor, no IME) are documented in
 * docs/adr/0006, not repeated here.
 */
class EditorViewport : public QWidget {
public:
    EditorViewport(AseBuffer *buffer, QString filePath, QWidget *parent = nullptr);
    ~EditorViewport() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void refreshCache();
    void drawLine(QPainter &painter, int start, int end, int y);
    void applyCaptureStyle(QPainter &painter, AseHighlightCapture capture);
    void insertText(const QByteArray &bytes);
    void deleteBackward();
    void deleteForward();
    void moveCursorLeft();
    void moveCursorRight();
    void moveCursorVertically(int lineDelta);
    void moveCursorHome();
    void moveCursorEnd();
    void ensureCursorVisible();
    void save();

    int lineForOffset(size_t offset) const;
    int columnForOffset(size_t offset, int line) const;
    size_t offsetForLineColumn(int line, int column) const;

    AseBuffer *m_buffer;
    QString m_filePath;

    AseSyntax *m_syntax = nullptr; /* null for unsupported file types — see docs/adr/0007 */
    QVector<AseHighlightSpan> m_highlights;

    QByteArray m_cache;
    QVector<int> m_lineStarts;

    size_t m_cursor = 0;
    int m_scrollLine = 0;
    int m_desiredColumn = -1;

    QFont m_font;
    int m_lineHeight = 0;
    int m_charWidth = 0;

    QTimer *m_blinkTimer;
    bool m_caretVisible = true;
};

#endif /* ASE_EDITOR_VIEWPORT_H */
