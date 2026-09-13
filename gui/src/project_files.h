#ifndef ASE_PROJECT_FILES_H
#define ASE_PROJECT_FILES_H

#include <QString>
#include <QStringList>

/*
 * Finding "the files in this project", for Ctrl+P — see docs/adr/0065.
 *
 * Deliberately not `git ls-files`. Spawning git would be faster on a
 * huge repository and would respect .gitignore for free, but it would
 * also mean Ctrl+P silently doing nothing in a directory that isn't a
 * git checkout, which is a normal way to use a text editor. Walking the
 * tree works everywhere and is the same answer in the common case.
 */
namespace project {

/* Nearest ancestor of `startDir` containing a `.git` entry, or
 * `startDir` itself if there is none. What "the project" means when you
 * open one file out of a checkout: its repository, not its directory. */
QString rootFor(const QString &startDir);

/*
 * Every file under `root`, as paths relative to it.
 *
 * Skips hidden entries and the directories that are always build
 * output or vendored dependencies — not a .gitignore parser, and not
 * pretending to be one. The cap is a hard stop, not a suggestion: this
 * runs on the UI thread and a stray Ctrl+P in `$HOME` must not freeze
 * the editor. `hitCap` reports that the listing is incomplete, so the
 * panel can say so rather than quietly lying.
 */
QStringList collect(const QString &root, int cap, bool *hitCap);

} // namespace project

#endif /* ASE_PROJECT_FILES_H */
