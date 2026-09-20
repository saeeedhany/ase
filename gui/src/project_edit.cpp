#include "project_edit.h"

#include <QSet>

namespace project {

QMap<QString, QVector<TextEdit>> editsByFile(const QVector<Replacement> &replacements,
                                              int needleLength, const QByteArray &replacement) {
    QMap<QString, QVector<TextEdit>> byFile;
    if (needleLength <= 0) {
        return byFile;
    }
    for (const Replacement &item : replacements) {
        if (!item.accepted) {
            continue;
        }
        byFile[item.hit.path].push_back(
            TextEdit{item.hit.line, item.hit.column, needleLength, replacement});
    }
    return byFile;
}

int acceptedCount(const QVector<Replacement> &replacements) {
    int count = 0;
    for (const Replacement &item : replacements) {
        if (item.accepted) {
            count++;
        }
    }
    return count;
}

int acceptedFileCount(const QVector<Replacement> &replacements) {
    QSet<QString> paths;
    for (const Replacement &item : replacements) {
        if (item.accepted) {
            paths.insert(item.hit.path);
        }
    }
    return paths.size();
}

} // namespace project
