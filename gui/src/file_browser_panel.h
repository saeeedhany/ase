#ifndef ASE_FILE_BROWSER_PANEL_H
#define ASE_FILE_BROWSER_PANEL_H

#include "floating_panel.h"

#include <QString>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class EditorViewport;
class LetterBadge;

/*
 * Open/Save-As as a centered floating panel, same family as FindBar —
 * see docs/adr/0022 (the design system) and docs/adr/0023 (this
 * panel). One badge ("O"/"S", swapped via LetterBadge::setLetter
 * rather than two separate badges) plus a path field that doubles as
 * the current-directory display, and a directory listing below it.
 * Owns only navigation/input; EditorViewport::openFile/saveAs do the
 * actual buffer work.
 */
class FileBrowserPanel : public FloatingPanel {
public:
    enum class Mode { Open, SaveAs };

    explicit FileBrowserPanel(EditorViewport *viewport);

    void openFor(Mode mode);
    void refreshTheme();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void hideBar();
    /* Lists `dir`'s entries (dirs first, then files, dotfiles excluded
     * — a v1 simplification, no toggle) and updates m_pathEdit to show
     * it as the implicit "current directory + type a name" field. */
    void setDirectory(const QString &dir);
    /* `name` is exactly one list entry's text (".." or "name"/"name/").
     * Directories navigate; a file either confirms (Open mode) or just
     * fills the path field without confirming (Save-As mode — avoids
     * an accidental overwrite from a stray double-click). */
    void activateEntry(const QString &name);
    /* Resolves `rawPath` against the current directory if relative,
     * navigates into it if it's a directory, otherwise commits: opens
     * (Open mode) or saves (Save-As mode) and closes the panel. Used by
     * both Enter-in-the-path-field and Open-mode file activation. */
    void confirmPath(const QString &rawPath);

    EditorViewport *m_viewport;
    Mode m_mode = Mode::Open;
    QString m_currentDir;

    LetterBadge *m_badge;
    QLineEdit *m_pathEdit;
    QListWidget *m_listWidget;
};

#endif /* ASE_FILE_BROWSER_PANEL_H */
