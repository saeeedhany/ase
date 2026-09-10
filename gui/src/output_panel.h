#ifndef ASE_OUTPUT_PANEL_H
#define ASE_OUTPUT_PANEL_H

#include <QWidget>

class QPlainTextEdit;
class EditorViewport;

/*
 * :compile's output — see docs/adr/0025. Deliberately *not* a
 * FloatingPanel: Find/Replace/Open/Save-As/the command line are all
 * "glance at it, act, dismiss" overlays that make sense centered on
 * top of your code, but you want to watch a build stream while still
 * looking at (and jumping back into) your code, not have it float over
 * it. A plain docked panel instead — a real layout row below
 * EditorViewport in main.cpp, not a child of it. Starts hidden until
 * the first :compile; toggleable after via `:output`.
 */
class OutputPanel : public QWidget {
public:
    explicit OutputPanel(EditorViewport *viewport, QWidget *parent = nullptr);

    void appendLine(const QString &text);
    /* No implied newline — used for streamed process output, which
     * arrives in arbitrary-sized chunks, not line-at-a-time. */
    void appendText(const QString &text);
    void clear();
    void refreshTheme();

private:
    EditorViewport *m_viewport;
    QPlainTextEdit *m_text;
};

#endif /* ASE_OUTPUT_PANEL_H */
