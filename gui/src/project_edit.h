#ifndef ASE_PROJECT_EDIT_H
#define ASE_PROJECT_EDIT_H

#include "project_search.h"
#include "text_edit.h"

#include <QMap>
#include <QString>
#include <QVector>

/*
 * Turning search hits into per-file edits — the half of replace-across-
 * files that can be wrong quietly, and therefore the half that is
 * testable on its own. What is left over in MainWindow is opening a
 * buffer and calling applyLineEdits(), which has nothing to get wrong.
 *
 * See docs/adr/0131.
 */
namespace project {

/* One entry per hit, in the order the results list shows them, so a
 * row's index is its index here. `accepted` is what the preview
 * toggles. */
struct Replacement {
    SearchHit hit;
    /* Per item, not per operation: a rename's WorkspaceEdit may replace
     * different lengths with different text in different places, and a
     * search-and-replace is only the special case where they all
     * match. */
    int length = 0;
    QByteArray replacement;
    QByteArray expected;
    bool accepted = true;
};

/* The accepted replacements, grouped by the file they belong to. */
QMap<QString, QVector<TextEdit>> editsByFile(const QVector<Replacement> &replacements);

/* How many files and how many hits the accepted set covers, for saying
 * what is about to happen before it happens. */
int acceptedCount(const QVector<Replacement> &replacements);
int acceptedFileCount(const QVector<Replacement> &replacements);

} // namespace project

#endif /* ASE_PROJECT_EDIT_H */
