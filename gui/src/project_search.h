#ifndef ASE_PROJECT_SEARCH_H
#define ASE_PROJECT_SEARCH_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

/*
 * Searching every file in the project for a literal string — see
 * docs/adr/0066. The candidate list comes from project::collect()
 * (gui/src/project_files.h), so quick open and this see exactly the
 * same files; a search that finds hits in files Ctrl+P cannot open
 * would be a bug in one of them.
 *
 * Case-insensitive literal matching, deliberately: it is what the
 * in-file find already does (EditorViewport::recomputeMatches), and two
 * searches in one editor that disagree about whether `Foo` matches
 * `foo` would be a worse surprise than either choice is a limitation.
 * No regular expressions yet.
 */
namespace project {

struct SearchHit {
    QString path; /* relative to the project root */
    int line;     /* 1-based, for display and for jumping */
    int column;   /* 1-based byte column of the match within the line */
    QString text; /* the whole line, for context in the results list */
};

struct SearchResult {
    QVector<SearchHit> hits;
    int filesSearched = 0;
    int filesSkipped = 0; /* binary, unreadable, or over the size cap */
    /* The hit cap was reached, so `hits` is a prefix of the truth. Said
     * out loud in the results header rather than silently dropping the
     * rest. */
    bool truncated = false;
};

SearchResult search(const QString &root, const QStringList &relativePaths, const QByteArray &needle,
                    int maxHits);

} // namespace project

#endif /* ASE_PROJECT_SEARCH_H */
