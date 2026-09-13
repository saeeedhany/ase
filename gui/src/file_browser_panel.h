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

/*
 * Open/Save-As as a centered floating panel, same family as FindBar —
 * see docs/adr/0022 (the design system), docs/adr/0023 (this panel),
 * and docs/adr/0024 (the search/highlight rework below). One badge
 * ("O"/"S", swapped via LetterBadge::setLetter rather than two
 * separate badges) plus a filter field — it shows only the *current
 * directory's name* as a placeholder, not a path to edit, and typing
 * into it filters the listing below rather than being parsed as a
 * literal path. Owns only navigation/input; EditorViewport::openFile/
 * saveAs do the actual buffer work.
 */
class FileBrowserPanel : public FloatingPanel {
public:
    /* QuickOpen (Ctrl+P) rides in this panel rather than getting a class
     * of its own: it is the same badge, field, list, sliding highlight,
     * theming and Enter/Escape handling, differing only in where the
     * entries come from and how they are filtered. A second widget would
     * have been ~200 duplicated lines that then drift apart visually.
     * See docs/adr/0065. */
    enum class Mode { Open, SaveAs, QuickOpen };

    explicit FileBrowserPanel(EditorViewport *viewport);

    void openFor(Mode mode);
    void refreshTheme();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void restoreFocusAfterDrag() override;

private:
    void hideBar();
    /* `~` and `~/...` expanded to the home directory, so a typed path
     * behaves the way it does in every shell and file dialog. Anything
     * else is returned unchanged. See docs/adr/0055. */
    static QString expandUser(const QString &path);
    /* True when `text` is meant as a path to resolve rather than a name
     * to filter by — it is absolute, starts with `~`, or contains a
     * separator. Typing a path used to work in Save-As and silently do
     * nothing in Open, which was the asymmetry that made this panel feel
     * wrong. */
    static bool looksLikePath(const QString &text);
    /* Lists `dir`'s entries (dirs first, then files, dotfiles excluded
     * — a v1 simplification, no toggle), clears the filter, and sets
     * the placeholder to `dir`'s own name (not the full path — you
     * search for a file by name, you don't read/edit a path string). */
    void setDirectory(const QString &dir);
    /* QuickOpen's equivalent of setDirectory(): finds the project root,
     * walks it once, and fills the list with every file in it, relative
     * to that root. */
    void setProjectRoot(const QString &startDir);
    /* Re-sorts the list by fuzzy score against `query` and keeps the
     * best kMaxQuickOpenRows — QuickOpen's answer to applyFilter(),
     * which only hides rows and cannot reorder them. */
    void applyQuickOpenFilter(const QString &query);
    /* Hides list rows that don't match `query` (case-insensitive
     * substring; ".." always stays visible) and selects the first
     * remaining match, so Enter picks whatever's fluently highlighted
     * without a separate confirm step. */
    void applyFilter(const QString &query);
    /* `name` is exactly one list entry's text (".." or "name"/"name/").
     * Directories navigate; a file either opens (Open mode) or fills
     * the filter field without confirming (Save-As mode — avoids an
     * accidental overwrite from a stray double-click). */
    void activateEntry(const QString &name);
    /* Themed yes/no, matching the window's own confirmations rather than
     * a native dialog (docs/adr/0044). */
    bool confirmOverwrite(const QString &path);
    /* Enter's behavior, split by mode: Open confirms whichever row is
     * currently highlighted in the (possibly filtered) list — there's
     * no reason to open a file that doesn't exist. Save-As instead
     * reads the filter field's own text as the filename to save
     * (resolved against the current directory), since typing a brand
     * new name that isn't in the listing yet is the whole point of
     * Save-As. */
    void confirmCurrent();
    /* The next non-hidden (i.e. not filtered-out) row from `fromRow`,
     * stepping by `step` (+1/-1), or -1 if none — used to forward
     * Up/Down from the filter field to the list, skipping filtered
     * rows the same way focus-on-the-list navigation already would. */
    int nextVisibleRow(int fromRow, int step) const;
    /* Moves (or, if not `animate`, snaps) the sliding highlight bar to
     * `row`'s rect — see docs/adr/0024. */
    void moveRowHighlight(int row, bool animate);

    EditorViewport *m_viewport;
    Mode m_mode = Mode::Open;
    QString m_currentDir;
    /* Collected once per Ctrl+P, then filtered in memory on every
     * keystroke — walking the tree per keystroke would be the obvious
     * way to make this feel slow. */
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
