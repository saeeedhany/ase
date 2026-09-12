#ifndef ASE_OUTPUT_PANEL_H
#define ASE_OUTPUT_PANEL_H

#include <QWidget>

class QPlainTextEdit;
class EditorViewport;
class TranslucentBar;

/*
 * :compile's output — see docs/adr/0025. Deliberately *not* a
 * FloatingPanel: Find/Replace/Open/Save-As/the command line are all
 * "glance at it, act, dismiss" overlays that make sense centered on
 * top of your code, but you want to watch a build stream while still
 * looking at (and jumping back into) your code, not have it float over
 * it. A plain docked panel instead — a real layout row below
 * EditorViewport in main.cpp, not a child of it. Starts hidden until
 * the first :compile; toggleable after via `:output`.
 *
 * A small seam marker (m_divider, a short TranslucentBar — not a
 * full-width rule) sits at this panel's own top edge, so it shows and
 * hides together with the panel itself with no extra wiring — see
 * docs/adr/0027. It's deliberately tiny: the user's own words were
 * "not even half a line," a quiet mark that this is a distinct panel,
 * not a heavy divider that would undercut the "reads as part of the
 * editor" simplicity they said they still want to keep.
 */
class OutputPanel : public QWidget {
public:
    explicit OutputPanel(EditorViewport *viewport, QWidget *parent = nullptr);

    /* One output panel is shared by every buffer (it shows the last
     * :compile, which belongs to the window, not to a file), so it gets
     * re-pointed at whichever viewport is active to pull theme colors
     * from. See docs/adr/0054. */
    void setViewport(EditorViewport *viewport);

    void appendLine(const QString &text);
    /* No implied newline — used for streamed process output, which
     * arrives in arbitrary-sized chunks, not line-at-a-time. */
    void appendText(const QString &text);
    void clear();
    void refreshTheme();

private:
    EditorViewport *m_viewport;
    TranslucentBar *m_divider;
    QPlainTextEdit *m_text;
};

#endif /* ASE_OUTPUT_PANEL_H */
