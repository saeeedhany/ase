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
 * :compile's output. Deliberately not a FloatingPanel: you watch a
 * build stream while still looking at your code, rather than having it
 * float over it. A docked layout row below EditorViewport, hidden until
 * the first :compile and toggled by `:output`.
 *
 * m_divider is a short seam marker at the top edge, so it shows and
 * hides with the panel and needs no wiring. See docs/adr/0025.
 */
class OutputPanel : public QWidget {
    Q_OBJECT

public:
    /* :compile streams text; a project search fills a list. Whichever
     * shows, the other is hidden. */
    enum class Mode { Output, SearchResults };
    explicit OutputPanel(EditorViewport *viewport, QWidget *parent = nullptr);

    /* Shared by every buffer, so it is re-pointed at the active
     * viewport to pull theme colours. */
    void setViewport(EditorViewport *viewport);

    /* Switches to the results list and fills it. `root` is kept so an
     * activated row can be turned back into an absolute path. */
    void showSearchResults(const QString &root, const QString &needle,
                            const project::SearchResult &result);
    /* The same list under a summary the caller writes — for results
     * that are places in the project but not search matches. */
    void showLocations(const QString &root, const QString &summary,
                        const QVector<project::SearchHit> &hits);

    void appendLine(const QString &text);
    /* No implied newline: process output arrives in arbitrary chunks. */
    void appendText(const QString &text);
    void clear();
    void refreshTheme();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    /* The results list has focus, so its keys are caught here. */
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    /* The window opens the file; this panel knows nothing of buffers. */
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
