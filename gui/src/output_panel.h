#ifndef ASE_OUTPUT_PANEL_H
#define ASE_OUTPUT_PANEL_H

#include "project_search.h"

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QKeyEvent;
class QLabel;
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
    Q_OBJECT

public:
    /* Two things live here, and they are the same shape: output you read
     * while looking at your code. `:compile` streams text; a project
     * search fills a list you pick from. Whichever is showing, the other
     * is hidden — see docs/adr/0066. */
    enum class Mode { Output, SearchResults };
    explicit OutputPanel(EditorViewport *viewport, QWidget *parent = nullptr);

    /* One output panel is shared by every buffer (it shows the last
     * :compile, which belongs to the window, not to a file), so it gets
     * re-pointed at whichever viewport is active to pull theme colors
     * from. See docs/adr/0054. */
    void setViewport(EditorViewport *viewport);

    /* Switches to the results list and fills it. `root` is kept so an
     * activated row can be turned back into an absolute path. */
    void showSearchResults(const QString &root, const QString &needle,
                            const project::SearchResult &result);

    void appendLine(const QString &text);
    /* No implied newline — used for streamed process output, which
     * arrives in arbitrary-sized chunks, not line-at-a-time. */
    void appendText(const QString &text);
    void clear();
    void refreshTheme();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    /* The results list has focus, so its keys never reach this widget's
     * own handler — Ctrl+J/K and Escape are caught here instead. */
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    /* A results row was picked. The window opens the file and jumps —
     * this panel deliberately knows nothing about buffers. */
    void hitActivated(const QString &absolutePath, int line);

private:
    void setMode(Mode mode);
    void activateRow(QListWidgetItem *item);

    EditorViewport *m_viewport;
    TranslucentBar *m_divider;
    QPlainTextEdit *m_text;
    QLabel *m_resultsHeader;
    QListWidget *m_results;
    Mode m_mode = Mode::Output;
    QString m_searchRoot;
    QVector<project::SearchHit> m_hits;
};

#endif /* ASE_OUTPUT_PANEL_H */
