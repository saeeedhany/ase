#include "ase/vcs.h"

#include "internal.h"

#include <stdlib.h>
#include <string.h>

/*
 * Only the hunk headers are read. Verified against real git output, one
 * case per shape:
 *
 *   insert 1 line after line 2      @@ -2,0 +3 @@
 *   insert 3 lines after line 2     @@ -2,0 +3,3 @@
 *   delete lines 3-4                @@ -3,2 +2,0 @@
 *   change line 2 in place          @@ -2 +2 @@
 *   replace 2 lines with 4          @@ -2,2 +2,4 @@
 *   replace 2 lines with 1          @@ -2,2 +2 @@
 *   delete the first line           @@ -1 +0,0 @@
 *   insert at the very top          @@ -0,0 +1 @@
 *
 * An omitted count means 1. A `+N,0` deletion has no line of its own, so
 * it is marked against line N — the last surviving line above the gap,
 * or the first line when the file's opening lines were the ones removed.
 */

typedef struct {
    long start; /* 0-based, inclusive */
    long count;
    AseVcsLineStatus status;
} Range;

struct AseVcsDiff {
    Range *ranges;
    size_t count;
    size_t capacity;
    size_t marked;
};

/* Beyond this a header is not describing a file anyone is editing, and
 * is more likely corrupt output than a real hunk. */
#define ASE_VCS_MAX_LINE 100000000L

static bool push(AseVcsDiff *diff, long start, long count, AseVcsLineStatus status) {
    if (count <= 0 || count > ASE_VCS_MAX_LINE || start < 0 || start > ASE_VCS_MAX_LINE) {
        return false;
    }
    if (diff->count == diff->capacity) {
        size_t next = diff->capacity == 0 ? 8 : diff->capacity * 2;
        Range *grown = (Range *)realloc(diff->ranges, next * sizeof(Range));
        if (grown == NULL) {
            return false;
        }
        diff->ranges = grown;
        diff->capacity = next;
    }
    diff->ranges[diff->count].start = start;
    diff->ranges[diff->count].count = count;
    diff->ranges[diff->count].status = status;
    diff->count++;
    diff->marked += (size_t)count;
    return true;
}

/* Digits only, bounded. Leaves `*cursor` on the first non-digit. */
static bool parse_long(const char **cursor, const char *end, long *out) {
    const char *p = *cursor;
    if (p >= end || *p < '0' || *p > '9') {
        return false;
    }
    long value = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        if (value > ASE_VCS_MAX_LINE) {
            return false; /* absurd, and about to overflow */
        }
        value = value * 10 + (*p - '0');
        p++;
    }
    *cursor = p;
    *out = value;
    return true;
}

/* "-12,3" or "-12": the count is 1 when omitted. */
static bool parse_range(const char **cursor, const char *end, char sign, long *start,
                        long *count) {
    if (*cursor >= end || **cursor != sign) {
        return false;
    }
    (*cursor)++;
    if (!parse_long(cursor, end, start)) {
        return false;
    }
    if (*cursor < end && **cursor == ',') {
        (*cursor)++;
        return parse_long(cursor, end, count);
    }
    *count = 1;
    return true;
}

static void parse_hunk(AseVcsDiff *diff, const char *line, const char *end) {
    const char *cursor = line + 2; /* past "@@" */
    while (cursor < end && *cursor == ' ') {
        cursor++;
    }
    long old_start = 0, old_count = 0, new_start = 0, new_count = 0;
    if (!parse_range(&cursor, end, '-', &old_start, &old_count)) {
        return;
    }
    while (cursor < end && *cursor == ' ') {
        cursor++;
    }
    if (!parse_range(&cursor, end, '+', &new_start, &new_count)) {
        return;
    }
    while (cursor < end && *cursor == ' ') {
        cursor++;
    }
    /* The closing "@@" — without it this was not a hunk header. */
    if (end - cursor < 2 || cursor[0] != '@' || cursor[1] != '@') {
        return;
    }

    /* Line 0 exists only as "before the first line", which is what a
     * pure deletion means. A non-empty range starting there is
     * malformed, and taking it at face value marked the file from a
     * negative offset. */
    if (new_start == 0 && new_count > 0) {
        return;
    }
    if (new_count == 0) {
        /* Removed lines have no line of their own; mark the one above
         * the gap, or the first line when the top of the file went. */
        long at = (new_start > 0) ? new_start - 1 : 0;
        push(diff, at, 1, ASE_VCS_DELETED);
        return;
    }
    if (old_count == 0) {
        push(diff, new_start - 1, new_count, ASE_VCS_ADDED);
        return;
    }
    /* Both sides non-empty: as many lines as were there are modified,
     * and anything past that is new. */
    long modified = (old_count < new_count) ? old_count : new_count;
    push(diff, new_start - 1, modified, ASE_VCS_MODIFIED);
    if (new_count > old_count) {
        push(diff, new_start - 1 + modified, new_count - old_count, ASE_VCS_ADDED);
    }
}

AseVcsDiff *ase_vcs_diff_parse(const char *diff_output, size_t len) {
    AseVcsDiff *diff = (AseVcsDiff *)calloc(1, sizeof(AseVcsDiff));
    if (diff == NULL || diff_output == NULL) {
        return diff;
    }

    const char *cursor = diff_output;
    const char *end = diff_output + len;
    while (cursor < end) {
        const char *newline = (const char *)memchr(cursor, '\n', (size_t)(end - cursor));
        const char *line_end = (newline != NULL) ? newline : end;
        if (line_end - cursor >= 2 && cursor[0] == '@' && cursor[1] == '@') {
            parse_hunk(diff, cursor, line_end);
        }
        if (newline == NULL) {
            break;
        }
        cursor = newline + 1;
    }
    return diff;
}

void ase_vcs_diff_destroy(AseVcsDiff *diff) {
    if (diff == NULL) {
        return;
    }
    free(diff->ranges);
    free(diff);
}

AseVcsLineStatus ase_vcs_diff_status(const AseVcsDiff *diff, long line) {
    if (diff == NULL || line < 0) {
        return ASE_VCS_UNCHANGED;
    }
    /* First match wins. git merges adjacent changes into one hunk — a
     * line changed next to lines removed comes back as a single
     * `@@ -1,3 +1 @@`, not as a modification and a deletion — so the
     * ranges never overlap and there is nothing to prefer between.
     * Hunk counts are small, so a scan beats an index. */
    for (size_t i = 0; i < diff->count; i++) {
        const Range *r = &diff->ranges[i];
        if (line >= r->start && line < r->start + r->count) {
            return r->status;
        }
    }
    return ASE_VCS_UNCHANGED;
}

size_t ase_vcs_diff_marked_count(const AseVcsDiff *diff) {
    return diff != NULL ? diff->marked : 0;
}
