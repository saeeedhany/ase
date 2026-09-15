#ifndef ASE_HELP_PANEL_H
#define ASE_HELP_PANEL_H

#include "floating_panel.h"

#include <QPair>
#include <QString>
#include <QVector>

class QLabel;
class SmoothLineEdit;
class QScrollArea;
class QVBoxLayout;
class EditorViewport;
class LetterBadge;

/*
 * Keybinding reference as a centered floating panel, same family as
 * the rest — see docs/adr/0022 (the design system) and docs/adr/0026
 * (this panel, added directly in response to user feedback once the
 * keybinding scheme itself settled — this panel's whole reason to
 * exist is documenting that scheme). "?" badge, `F1` opens it.
 * Sections collapse, and a search field filters across every binding —
 * see docs/adr/0056. Shortcuts are the primary way this editor is
 * driven, so the reference is something you come back to and scan, not
 * read once; one long wall of rich text made you hunt. Escape closes
 * it (or clears the search first, if there is one). The body text is
 * maintained by hand here rather than generated from the keybinding
 * dispatch code — the two can drift; that's an accepted v1 tradeoff
 * (a generated single source of truth is real, unscoped work).
 */
class HelpPanel : public FloatingPanel {
public:
    explicit HelpPanel(EditorViewport *viewport);

    void openHelp();
    void refreshTheme();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void restoreFocusAfterDrag() override;

private:
    /* One collapsible section: a clickable title row plus its rows of
     * bindings. Kept as widgets rather than one rich-text blob so a
     * section can actually be collapsed and filtered. */
    struct Section {
        QString title;
        QString note;
        QWidget *header = nullptr;
        QLabel *titleLabel = nullptr;
        QLabel *rowsLabel = nullptr;
        QWidget *rowsWidget = nullptr;
        QVector<QPair<QString, QString>> rows;
        bool expanded = true;
        bool visible = true;
    };

    void buildSections();
    void applySearch(const QString &query);
    /* Held so restyleSections can drop non-matching rows. */
    QString m_searchNeedle;
    void setSectionExpanded(int index, bool expanded);
    void restyleSections();

    SmoothLineEdit *m_search = nullptr;
    QWidget *m_sectionsHost = nullptr;
    QVBoxLayout *m_sectionsLayout = nullptr;
    QVector<Section> m_sections;

    void hideBar();

    EditorViewport *m_viewport;
    LetterBadge *m_badge;
    QLabel *m_title;
    QScrollArea *m_scrollArea;
};

#endif /* ASE_HELP_PANEL_H */
