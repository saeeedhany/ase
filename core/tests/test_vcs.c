#include "test_assert.h"
#include <stdio.h>
#include <string.h>

#include "ase/vcs.h"

static AseVcsDiff *parse(const char *text) {
    return ase_vcs_diff_parse(text, strlen(text));
}

static void expect(const char *label, const AseVcsDiff *diff, long line,
                   AseVcsLineStatus want) {
    AseVcsLineStatus got = ase_vcs_diff_status(diff, line);
    if (got != want) {
        printf("%s: line %ld is %d, expected %d\n", label, line, (int)got, (int)want);
        CHECK(0);
    }
}

/* `@@ -1,0 +2,3 @@` — nothing was there, three lines are now. */
static void test_pure_addition(void) {
    AseVcsDiff *d = parse("@@ -1,0 +2,3 @@\n");
    CHECK(d != NULL);
    expect("added", d, 0, ASE_VCS_UNCHANGED);
    expect("added", d, 1, ASE_VCS_ADDED);
    expect("added", d, 2, ASE_VCS_ADDED);
    expect("added", d, 3, ASE_VCS_ADDED);
    expect("added", d, 4, ASE_VCS_UNCHANGED);
    CHECK(ase_vcs_diff_marked_count(d) == 3);
    ase_vcs_diff_destroy(d);
}

/* `@@ -5,3 +4,0 @@` — three lines went, and there is no line left to
 * mark, so the mark goes on the one above the gap. */
static void test_pure_deletion(void) {
    AseVcsDiff *d = parse("@@ -5,3 +4,0 @@\n");
    CHECK(d != NULL);
    expect("deleted", d, 3, ASE_VCS_DELETED);
    expect("deleted", d, 4, ASE_VCS_UNCHANGED);
    ase_vcs_diff_destroy(d);
}

/* A deletion at the very top has no line above it; it belongs on the
 * first line rather than nowhere. */
static void test_deletion_at_the_start(void) {
    AseVcsDiff *d = parse("@@ -1,2 +0,0 @@\n");
    CHECK(d != NULL);
    expect("deleted at top", d, 0, ASE_VCS_DELETED);
    ase_vcs_diff_destroy(d);
}

static void test_modification(void) {
    AseVcsDiff *d = parse("@@ -3,2 +3,2 @@\n");
    CHECK(d != NULL);
    expect("modified", d, 1, ASE_VCS_UNCHANGED);
    expect("modified", d, 2, ASE_VCS_MODIFIED);
    expect("modified", d, 3, ASE_VCS_MODIFIED);
    expect("modified", d, 4, ASE_VCS_UNCHANGED);
    ase_vcs_diff_destroy(d);
}

/* Two old lines became five: two modified, three added. */
static void test_modification_that_grows(void) {
    AseVcsDiff *d = parse("@@ -3,2 +3,5 @@\n");
    CHECK(d != NULL);
    expect("grew", d, 2, ASE_VCS_MODIFIED);
    expect("grew", d, 3, ASE_VCS_MODIFIED);
    expect("grew", d, 4, ASE_VCS_ADDED);
    expect("grew", d, 5, ASE_VCS_ADDED);
    expect("grew", d, 6, ASE_VCS_ADDED);
    expect("grew", d, 7, ASE_VCS_UNCHANGED);
    ase_vcs_diff_destroy(d);
}

/* Five old lines became two: two modified, and the rest deleted — the
 * gap is marked on the last surviving line. */
static void test_modification_that_shrinks(void) {
    AseVcsDiff *d = parse("@@ -3,5 +3,2 @@\n");
    CHECK(d != NULL);
    expect("shrank", d, 2, ASE_VCS_MODIFIED);
    expect("shrank", d, 3, ASE_VCS_MODIFIED);
    expect("shrank", d, 4, ASE_VCS_UNCHANGED);
    ase_vcs_diff_destroy(d);
}

/* An omitted count means 1 — git writes `@@ -4 +4 @@` for a one-line
 * change, and reading that as 0 would mark nothing. */
static void test_omitted_counts_mean_one(void) {
    AseVcsDiff *d = parse("@@ -4 +4 @@\n");
    CHECK(d != NULL);
    expect("one line", d, 3, ASE_VCS_MODIFIED);
    expect("one line", d, 2, ASE_VCS_UNCHANGED);
    expect("one line", d, 4, ASE_VCS_UNCHANGED);
    ase_vcs_diff_destroy(d);

    AseVcsDiff *added = parse("@@ -0,0 +1 @@\n");
    CHECK(added != NULL);
    expect("one added", added, 0, ASE_VCS_ADDED);
    ase_vcs_diff_destroy(added);
}

/* Real output, with the header git prints before the hunks and the
 * function-context suffix it prints after them. */
static void test_real_git_output(void) {
    const char *real =
        "diff --git a/core/src/session.c b/core/src/session.c\n"
        "index 9beae3d..1a2b3c4 100644\n"
        "--- a/core/src/session.c\n"
        "+++ b/core/src/session.c\n"
        "@@ -12,2 +12,3 @@ struct AseSession {\n"
        "-    size_t count;\n"
        "+    size_t count;\n"
        "+    size_t capacity;\n"
        "@@ -40,0 +42,2 @@ void ase_session_destroy(AseSession *session) {\n"
        "+    free(session->entries);\n"
        "+    free(session);\n";
    AseVcsDiff *d = ase_vcs_diff_parse(real, strlen(real));
    CHECK(d != NULL);
    expect("real", d, 11, ASE_VCS_MODIFIED);
    expect("real", d, 12, ASE_VCS_MODIFIED);
    expect("real", d, 13, ASE_VCS_ADDED);
    expect("real", d, 41, ASE_VCS_ADDED);
    expect("real", d, 42, ASE_VCS_ADDED);
    expect("real", d, 20, ASE_VCS_UNCHANGED);
    ase_vcs_diff_destroy(d);
}

/* `---`/`+++` lines start with the same characters as a hunk header's
 * fields; mistaking one for a hunk would mark the whole file. */
static void test_file_headers_are_not_hunks(void) {
    AseVcsDiff *d = parse("--- a/x.c\n+++ b/x.c\n");
    CHECK(d != NULL);
    CHECK(ase_vcs_diff_marked_count(d) == 0);
    ase_vcs_diff_destroy(d);
}

static void test_unmodified_file_has_no_marks(void) {
    AseVcsDiff *d = parse("");
    CHECK(d != NULL);
    CHECK(ase_vcs_diff_marked_count(d) == 0);
    expect("empty", d, 0, ASE_VCS_UNCHANGED);
    ase_vcs_diff_destroy(d);
}

/* Anything can end up on stdout — a git error, a pager, truncation. */
static void test_garbage_is_survivable(void) {
    static const char *kJunk[] = {
        "@@",
        "@@ @@\n",
        "@@ -a,b +c,d @@\n",
        "@@ -1,1 +\n",
        "@@ -1,1 +1,1\n",       /* no trailing @@ */
        "fatal: not a git repository\n",
        "@@ -999999999999999999999,1 +1,1 @@\n",
        "@@ -1,1 +0,99999999 @@\n",
    };
    for (size_t i = 0; i < sizeof(kJunk) / sizeof(kJunk[0]); i++) {
        AseVcsDiff *d = parse(kJunk[i]);
        CHECK(d != NULL);
        /* Not merely "does not crash": none of these is a hunk, so none
         * of them may mark a line. A header without its closing @@ is
         * truncated output, not a change. */
        if (ase_vcs_diff_marked_count(d) != 0) {
            printf("garbage marked %zu lines: %s\n", ase_vcs_diff_marked_count(d), kJunk[i]);
            CHECK(0);
        }
        CHECK(ase_vcs_diff_status(d, 0) == ASE_VCS_UNCHANGED);
        CHECK(ase_vcs_diff_status(d, 100000) == ASE_VCS_UNCHANGED);
        CHECK(ase_vcs_diff_status(d, -5) == ASE_VCS_UNCHANGED);
        ase_vcs_diff_destroy(d);
    }
}

static void test_null_is_survivable(void) {
    CHECK(ase_vcs_diff_status(NULL, 3) == ASE_VCS_UNCHANGED);
    CHECK(ase_vcs_diff_marked_count(NULL) == 0);
    ase_vcs_diff_destroy(NULL);
}

int main(void) {
    test_pure_addition();
    test_pure_deletion();
    test_deletion_at_the_start();
    test_modification();
    test_modification_that_grows();
    test_modification_that_shrinks();
    test_omitted_counts_mean_one();
    test_real_git_output();
    test_file_headers_are_not_hunks();
    test_unmodified_file_has_no_marks();
    test_garbage_is_survivable();
    test_null_is_survivable();

    printf("all vcs tests passed\n");
    return 0;
}
