#ifndef ASE_RECOVERY_H
#define ASE_RECOVERY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Unsaved work, kept somewhere it survives a crash.
 *
 * A snapshot rather than a log of edits: the editor's buffer is a piece
 * table whose contents are cheap to serialise, and replaying a log is a
 * second implementation of editing that can disagree with the first.
 * The snapshot cannot disagree with anything — it is the buffer.
 *
 * Snapshots live in one directory, named after a hash of the file's
 * path, so they never appear beside the user's own files and cannot end
 * up committed. The path is stored inside each snapshot and checked on
 * read, so a hash collision reads as "no recovery" rather than as
 * someone else's text.
 */

/* Records `content` as the unsaved state of `file_path`. Writes
 * atomically, so an interrupted snapshot leaves the previous one. */
bool ase_recovery_write(const char *dir, const char *file_path, const char *content, size_t len);

/* The recorded content for `file_path`, or NULL when there is none.
 * Sets `*out_len` when it returns non-NULL. Caller frees. */
char *ase_recovery_read(const char *dir, const char *file_path, size_t *out_len);

/* True when a snapshot for `file_path` is on disk. */
bool ase_recovery_exists(const char *dir, const char *file_path);

/* Drops the snapshot for `file_path`. True when none remains, whether
 * or not there was one to remove. */
bool ase_recovery_remove(const char *dir, const char *file_path);

#ifdef __cplusplus
}
#endif

#endif
