#ifndef ASE_LSP_WORKSPACE_EDIT_H
#define ASE_LSP_WORKSPACE_EDIT_H

#include "project_edit.h"

#include <QDir>
#include <QVector>

extern "C" {
#include "ase/json.h"
}

/*
 * Reading a WorkspaceEdit — what a server says a rename would change.
 *
 * Its own unit because it is the part with edge cases: two shapes to
 * accept, ranges that cannot be represented, and a coordinate system
 * that is not this editor's. See docs/adr/0132.
 */
namespace lsp {

/*
 * Every edit the reply holds, as replacements the preview and apply
 * path already understand.
 *
 * Paths come back relative to `root`. `expected` is what must already
 * be at each position — the old name — and is carried through so a
 * range that does not describe the file is skipped rather than applied.
 *
 * A multi-line range is dropped, not approximated: rename does not
 * produce one for a name, and guessing at what a server meant by one is
 * how a refactor eats a function body.
 */
QVector<project::Replacement> replacementsFrom(const AseJsonValue *workspaceEdit, const QDir &root,
                                                const QByteArray &expected);

} // namespace lsp

#endif /* ASE_LSP_WORKSPACE_EDIT_H */
