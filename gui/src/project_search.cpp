#include "project_search.h"

#include <cctype>

#include <QDir>
#include <QFile>

namespace {

/* Files larger than this are skipped. A megabyte of source is already
 * far outside what this editor is built to open comfortably, and the
 * things that actually hit this limit — minified bundles, generated
 * headers, test fixtures full of data — are not what anyone is
 * searching for. */
constexpr qint64 kMaxFileBytes = 1 << 20;

/* How much of a file to examine before deciding it is binary. A NUL in
 * the first kilobyte is the same heuristic grep uses, and for the same
 * reason: it is cheap, and text files do not contain NUL. */
constexpr int kBinarySniffBytes = 1024;

bool looksBinary(const QByteArray &contents) {
    int limit = std::min(static_cast<int>(contents.size()), kBinarySniffBytes);
    for (int i = 0; i < limit; ++i) {
        if (contents[i] == '\0') {
            return true;
        }
    }
    return false;
}

} // namespace

project::SearchResult project::search(const QString &root, const QStringList &relativePaths,
                                       const QByteArray &needle, int maxHits, bool everyOccurrence) {
    SearchResult result;
    if (needle.isEmpty()) {
        return result;
    }

    QDir rootDir(root);
    const QByteArray needleLower = needle.toLower();

    for (const QString &relative : relativePaths) {
        QFile file(rootDir.filePath(relative));
        if (file.size() > kMaxFileBytes || !file.open(QIODevice::ReadOnly)) {
            result.filesSkipped++;
            continue;
        }
        const QByteArray contents = file.readAll();
        file.close();
        if (looksBinary(contents)) {
            result.filesSkipped++;
            continue;
        }
        result.filesSearched++;

        /* Lowercased once per file, not once per match attempt — the
         * same trick the in-file search uses, and the difference
         * between searching a tree and waiting for one. */
        const QByteArray haystack = contents.toLower();
        int from = 0;
        int line = 1;
        int lineStart = 0;
        int scanned = 0; /* how far the line counter has advanced */

        while (true) {
            int at = haystack.indexOf(needleLower, from);
            if (at < 0) {
                break;
            }
            /* Walk the newline counter forward to the hit rather than
             * re-counting from the start of the file for every match. */
            for (; scanned < at; ++scanned) {
                if (contents[scanned] == '\n') {
                    line++;
                    lineStart = scanned + 1;
                }
            }
            int lineEnd = contents.indexOf('\n', at);
            if (lineEnd < 0) {
                lineEnd = contents.size();
            }

            SearchHit hit;
            hit.path = relative;
            hit.line = line;
            hit.column = at - lineStart + 1;
            const QByteArray rawLine = contents.mid(lineStart, lineEnd - lineStart);
            /* How much indentation trimmed() will take off the front,
             * so the match can be found again in what is displayed. */
            int leading = 0;
            while (leading < rawLine.size() && isspace(static_cast<unsigned char>(rawLine[leading]))) {
                leading++;
            }
            hit.text = QString::fromUtf8(rawLine).trimmed();
            hit.textColumn = (at - lineStart) - leading + 1;
            result.hits.push_back(hit);

            if (result.hits.size() >= maxHits) {
                result.truncated = true;
                return result;
            }
            /* One hit per line: a line containing the needle six times
             * is one place to look, and six identical rows in the
             * results list is noise that pushes real hits off screen. */
            /* One per line for a search, every one for a replace — see
             * the header. */
            from = everyOccurrence ? at + needleLower.size() : lineEnd + 1;
        }
    }

    return result;
}
