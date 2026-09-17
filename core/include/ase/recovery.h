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

/*
 * Every snapshot in `dir`: the key recorded inside each one, and the
 * file it lives in. Parallel arrays of `count` entries.
 *
 * The key is a file path for a saved file, and whatever the caller used
 * for a buffer that has none — an unnamed buffer is exactly the case
 * with unsaved work and nothing on disk to fall back to, and it can
 * only be offered back by walking the directory, because nothing will
 * ever "reopen" it. See docs/adr/0127.
 */
typedef struct {
    char **keys;
    char **paths;
    size_t count;
} AseRecoveryList;

/* False only on a directory that cannot be read; an empty or missing
 * one lists nothing and succeeds. Free with ase_recovery_list_free(). */
bool ase_recovery_list(const char *dir, AseRecoveryList *out);
void ase_recovery_list_free(AseRecoveryList *list);

/* Removes snapshots untouched for longer than `max_age_seconds`,
 * returning how many went. A snapshot for a file crashed on and never
 * reopened would otherwise stay for good. */
size_t ase_recovery_prune(const char *dir, long max_age_seconds);

#ifdef __cplusplus
}
#endif

#endif
