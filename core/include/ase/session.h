#ifndef ASE_SESSION_H
#define ASE_SESSION_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What was open last time: the files, where the caret was in each, and
 * which one was in front. Enough to put the editor back where it was
 * without storing the text — the files are on disk, and anything unsaved
 * is ase/recovery.h's problem, not this one.
 */

typedef struct {
    char *path;
    size_t cursor;   /* byte offset, clamped on load by the caller */
    long scroll_line;
} AseSessionEntry;

typedef struct AseSession AseSession;

AseSession *ase_session_create(void);
void ase_session_destroy(AseSession *session);

/* Appends an entry. A NULL or empty path is ignored — an unnamed buffer
 * has nothing to reopen. */
bool ase_session_add(AseSession *session, const char *path, size_t cursor, long scroll_line);

size_t ase_session_count(const AseSession *session);
const AseSessionEntry *ase_session_entry(const AseSession *session, size_t index);

/* Which entry was in front. Out of range reads back as 0. */
void ase_session_set_active(AseSession *session, size_t index);
size_t ase_session_active(const AseSession *session);

/* Written atomically; a half-written session file would be worse than
 * none. Saving an empty session removes the file instead. */
bool ase_session_save(const AseSession *session, const char *path);

/* NULL when there is nothing to restore, including when the file is
 * missing or unreadable. Lines that do not parse are skipped rather
 * than failing the whole restore. */
AseSession *ase_session_load(const char *path);

#ifdef __cplusplus
}
#endif

#endif
