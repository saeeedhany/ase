#ifndef ASE_FILE_BROWSER_PANEL_H
#define ASE_FILE_BROWSER_PANEL_H

#include "floating_panel.h"
#include "translucent_bar.h"

#include <QString>
#include <QStringList>

class QLabel;
class SmoothLineEdit;
class QListWidget;
class QListWidgetItem;
class QPropertyAnimation;
class EditorViewport;
class LetterBadge;

/* Open/Save-As as a floating panel. Owns navigation and input only;
 * EditorViewport does the buffer work. See docs/adr/0023. */
class FileBrowserPanel : public FloatingPanel {
public:
    /* QuickOpen rides in this panel: same badge, field, list and
     * handling, differing only in where entries come from. */
    enum class Mode { Open, SaveAs, QuickOpen };

    explicit FileBrowserPanel(EditorViewport *viewport);

    void openFor(Mode mode);
    void refreshTheme();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void restoreFocusAfterDrag() override;

private:
    void hideBar();
    /* Expands `~`; anything else is returned unchanged. */
    static QString expandUser(const QString &path);
    /* A path to resolve rather than a name to filter by: absolute,
     * starting with `~`, or containing a separator. */
    static bool looksLikePath(const QString &text);
    /* Dirs first, then files; dotfiles excluded. */
    void setDirectory(const QString &dir);
    /* QuickOpen's setDirectory(): walks the project root once. */
    void setProjectRoot(const QString &startDir);
    QString showCurrentDir();
    /* Re-sorts by fuzzy score; applyFilter() can only hide rows. */
    void applyQuickOpenFilter(const QString &query);
    /* Case-insensitive substring; ".." always stays visible. Selects
     * the first match, so Enter needs no separate confirm. */
    void applyFilter(const QString &query);
    /* Directories navigate; a file opens, or in Save-As only fills the
     * field, so a stray double-click cannot overwrite. */
    void activateEntry(const QString &name);
    /* Themed, matching the window's own confirmations. */
    bool confirmOverwrite(const QString &path);
    /* Open confirms the highlighted row; Save-As reads the field,
     * since a new name is the whole point. */
    void confirmCurrent();
    /* Next non-hidden row, stepping by +1/-1, or -1 if none. */
    int nextVisibleRow(int fromRow, int step) const;
    /* Moves, or snaps, the highlight bar to `row`. */
    void moveRowHighlight(int row, bool animate);

    EditorViewport *m_viewport;
    Mode m_mode = Mode::Open;
    QString m_currentDir;
    /* Walked once per Ctrl+P, then filtered in memory. */
    QStringList m_projectFiles;
    bool m_projectFilesTruncated = false;

    LetterBadge *m_badge;
    QLabel *m_pathLabel;
    SmoothLineEdit *m_filterEdit;
    QListWidget *m_listWidget;
    TranslucentBar *m_rowHighlight;
    QPropertyAnimation *m_rowHighlightAnim;
};

#endif /* ASE_FILE_BROWSER_PANEL_H */
