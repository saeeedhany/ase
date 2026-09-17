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
class PanelResizeHandle;
class QPropertyAnimation;
class QVariantAnimation;

/*
 * :compile's output. Deliberately not a FloatingPanel: you watch a
 * build stream while still looking at your code, rather than having it
 * float over it. A docked layout row below EditorViewport, hidden until
 * the first :compile and toggled by `:output`.
 *
 * m_handle is the seam at the top edge — a hairline that dims when
 * unattended and doubles as the drag grip for the panel's height. It
 * shows and hides with the panel and needs no wiring. See
 * docs/adr/0025 and docs/adr/0117.
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

    /* Taller/shorter by one step, animated. Bound to keys so the panel
     * can be sized without reaching for the mouse — see docs/adr/0117. */
    void growBy(int delta);

    /* Move the keyboard into the panel, and ask whether it is already
     * there — the editor needs both to decide what a toggle means. */
    void focusList();
    bool hasFocusInside() const;

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
    void resizeByDrag(int delta);
    void selectRowSmoothly(int row);
    void applyHeight(int height, bool animated);
    void setMode(Mode mode);
    void activateRow(QListWidgetItem *item);

    EditorViewport *m_viewport;
    PanelResizeHandle *m_handle = nullptr;
    /* The panel's own height, once the user has chosen one; -1 means
     * "whatever the layout decides". */
    int m_chosenHeight = -1;
    QPropertyAnimation *m_scroll = nullptr;
    QVariantAnimation *m_resize = nullptr;
    QPlainTextEdit *m_text;
    QLabel *m_resultsHeader;
    QListWidget *m_results;
    Mode m_mode = Mode::Output;
    QString m_searchRoot;
    QVector<project::SearchHit> m_hits;
};

#endif /* ASE_OUTPUT_PANEL_H */
