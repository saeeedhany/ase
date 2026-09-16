#ifndef ASE_VCS_H
#define ASE_VCS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What changed in this file since the last commit, per line, for the
 * gutter. Parsed from `git diff -U0`'s hunk headers — the only part of
 * the output that is needed, and the only part whose format git treats
 * as stable.
 *
 * A header reads `@@ -old,oldcount +new,newcount @@`, with the count
 * omitted when it is 1. From it:
 *
 *   oldcount == 0   lines were added at new
 *   newcount == 0   lines were deleted; marked on the line above the gap
 *   both non-zero   lines were modified, and any excess new lines added
 */

typedef enum {
    ASE_VCS_UNCHANGED = 0,
    ASE_VCS_ADDED,
    ASE_VCS_MODIFIED,
    /* A gap, not a line: drawn against the line above where text was
     * removed, since the removed lines have no line of their own. */
    ASE_VCS_DELETED,
} AseVcsLineStatus;

typedef struct AseVcsDiff AseVcsDiff;

/* Parses the hunk headers out of `diff_output`. Never NULL except on
 * allocation failure; output that contains no hunks parses to an empty
 * diff, which is what an unmodified file produces. */
AseVcsDiff *ase_vcs_diff_parse(const char *diff_output, size_t len);
void ase_vcs_diff_destroy(AseVcsDiff *diff);

/* `line` is 0-based. Lines outside any hunk are unchanged. */
AseVcsLineStatus ase_vcs_diff_status(const AseVcsDiff *diff, long line);

/* How many lines carry a mark — 0 means the file matches the commit. */
size_t ase_vcs_diff_marked_count(const AseVcsDiff *diff);

#ifdef __cplusplus
}
#endif

#endif
