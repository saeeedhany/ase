#include "project_files.h"

#include <QDir>
#include <QFileInfo>

namespace {

/*
 * Directories never worth walking into. Build output and vendored
 * dependencies dwarf the source in both file count and depth, and
 * nobody opens them with Ctrl+P — the one that matters most here is
 * `.git`, which on this repository alone holds more files than the
 * entire working tree.
 *
 * A list, not a .gitignore parser: this is a fixed set of well-known
 * names, it is honest about being that, and it never surprises you by
 * hiding a file you can see in your own directory listing.
 */
bool isSkippedDirectory(const QString &name) {
    static const QStringList skipped = {
        QStringLiteral(".git"),        QStringLiteral(".svn"),      QStringLiteral(".hg"),
        QStringLiteral("node_modules"), QStringLiteral("__pycache__"), QStringLiteral(".mypy_cache"),
        QStringLiteral(".pytest_cache"), QStringLiteral(".cache"),   QStringLiteral("dist"),
        QStringLiteral("target"),      QStringLiteral("site"),       QStringLiteral(".venv"),
        QStringLiteral("venv"),
    };
    if (skipped.contains(name)) {
        return true;
    }
    /* Covers build, build-deb, build-appimage, .venv-docs — every
     * convention this project's own tree uses, without listing each. */
    return name.startsWith(QLatin1String("build")) || name.startsWith(QLatin1String(".venv"));
}

} // namespace

QString project::rootFor(const QString &startDir) {
    QDir dir(startDir);
    while (true) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral(".git")))) {
            return dir.absolutePath();
        }
        if (dir.isRoot() || !dir.cdUp()) {
            return QDir(startDir).absolutePath();
        }
    }
}

QStringList project::collect(const QString &root, int cap, bool *hitCap) {
    QStringList files;
    if (hitCap != nullptr) {
        *hitCap = false;
    }

    QDir rootDir(root);
    /* An explicit stack rather than QDirIterator::Subdirectories,
     * because that descends into every directory it lists and only lets
     * you filter the *results* — on this repository that means walking
     * all of .git to throw it away. Pruning at the directory is the
     * whole point. */
    QStringList pending;
    pending.push_back(rootDir.absolutePath());

    while (!pending.isEmpty()) {
        QDir dir(pending.takeLast());
        /* NoSymLinks matters: a symlink pointing at an ancestor turns
         * this into an endless walk, with the cap as the only brake. */
        const QFileInfoList entries =
            dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                              QDir::Name);
        for (const QFileInfo &info : entries) {
            const QString name = info.fileName();
            if (name.startsWith(QLatin1Char('.'))) {
                continue;
            }
            if (info.isDir()) {
                if (!isSkippedDirectory(name)) {
                    pending.push_back(info.absoluteFilePath());
                }
                continue;
            }
            files.push_back(rootDir.relativeFilePath(info.absoluteFilePath()));
            if (files.size() >= cap) {
                if (hitCap != nullptr) {
                    *hitCap = true;
                }
                return files;
            }
        }
    }
    return files;
}
