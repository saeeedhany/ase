#include "test_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/session.h"

static const char *kPath = "test_session.tmp";

static void test_round_trip(void) {
    AseSession *session = ase_session_create();
    CHECK(session != NULL);
    CHECK(ase_session_add(session, "/home/u/one.c", 42, 3));
    CHECK(ase_session_add(session, "/home/u/two.c", 0, 0));
    CHECK(ase_session_add(session, "/home/u/three.c", 999, 120));
    ase_session_set_active(session, 2);
    CHECK(ase_session_save(session, kPath));
    ase_session_destroy(session);

    AseSession *back = ase_session_load(kPath);
    CHECK(back != NULL);
    CHECK(ase_session_count(back) == 3);
    CHECK(ase_session_active(back) == 2);

    const AseSessionEntry *first = ase_session_entry(back, 0);
    CHECK(first != NULL);
    CHECK(strcmp(first->path, "/home/u/one.c") == 0);
    CHECK(first->cursor == 42);
    CHECK(first->scroll_line == 3);

    const AseSessionEntry *third = ase_session_entry(back, 2);
    CHECK(third != NULL);
    CHECK(strcmp(third->path, "/home/u/three.c") == 0);
    CHECK(third->cursor == 999);
    CHECK(third->scroll_line == 120);

    ase_session_destroy(back);
    remove(kPath);
}

/* Order is the tab order, so it has to survive a round trip. */
static void test_order_is_kept(void) {
    AseSession *session = ase_session_create();
    const char *names[] = {"/a", "/b", "/c", "/d", "/e"};
    for (size_t i = 0; i < 5; i++) {
        CHECK(ase_session_add(session, names[i], i, (long)i));
    }
    CHECK(ase_session_save(session, kPath));
    ase_session_destroy(session);

    AseSession *back = ase_session_load(kPath);
    CHECK(back != NULL);
    CHECK(ase_session_count(back) == 5);
    for (size_t i = 0; i < 5; i++) {
        const AseSessionEntry *e = ase_session_entry(back, i);
        CHECK(e != NULL && strcmp(e->path, names[i]) == 0);
    }
    ase_session_destroy(back);
    remove(kPath);
}

/* Paths carry spaces, '=' and unicode; none of it should confuse the
 * parser. */
static void test_awkward_paths(void) {
    AseSession *session = ase_session_create();
    CHECK(ase_session_add(session, "/home/u/my documents/a file.c", 1, 0));
    CHECK(ase_session_add(session, "/home/u/weird=name.c", 2, 0));
    CHECK(ase_session_add(session, "/home/u/\xc3\xa9t\xc3\xa9.c", 3, 0));
    CHECK(ase_session_save(session, kPath));
    ase_session_destroy(session);

    AseSession *back = ase_session_load(kPath);
    CHECK(back != NULL);
    CHECK(ase_session_count(back) == 3);
    CHECK(strcmp(ase_session_entry(back, 0)->path, "/home/u/my documents/a file.c") == 0);
    CHECK(strcmp(ase_session_entry(back, 1)->path, "/home/u/weird=name.c") == 0);
    CHECK(strcmp(ase_session_entry(back, 2)->path, "/home/u/\xc3\xa9t\xc3\xa9.c") == 0);
    ase_session_destroy(back);
    remove(kPath);
}

/* An unnamed buffer has nothing to reopen. */
static void test_unnamed_buffers_are_skipped(void) {
    AseSession *session = ase_session_create();
    CHECK(!ase_session_add(session, NULL, 0, 0));
    CHECK(!ase_session_add(session, "", 0, 0));
    CHECK(ase_session_count(session) == 0);
    ase_session_destroy(session);
}

/* Nothing open means nothing to restore, and no stale file left saying
 * otherwise. */
static void test_empty_session_removes_the_file(void) {
    AseSession *session = ase_session_create();
    CHECK(ase_session_add(session, "/a", 0, 0));
    CHECK(ase_session_save(session, kPath));
    ase_session_destroy(session);
    AseSession *present = ase_session_load(kPath);
    CHECK(present != NULL);
    ase_session_destroy(present);

    AseSession *empty = ase_session_create();
    CHECK(ase_session_save(empty, kPath));
    ase_session_destroy(empty);

    CHECK(ase_session_load(kPath) == NULL);
    CHECK(fopen(kPath, "rb") == NULL);
}

static void test_missing_file_loads_as_nothing(void) {
    CHECK(ase_session_load("test_session_does_not_exist.tmp") == NULL);
}

/* A session file that has been truncated or hand-edited must not take
 * the editor down with it. */
static void test_malformed_input_is_survivable(void) {
    static const char *kJunk[] = {
        "",
        "not a session file at all\n",
        "ASE-SESSION 1\nactive = 99\n",
        "ASE-SESSION 1\nfile = /a\ncursor = notanumber\n",
        "ASE-SESSION 1\ncursor = 5\nscroll = 5\n", /* values with no file */
        "ASE-SESSION 1\nfile = /a\nfile = /b\n",
        "ASE-SESSION 1\nfile =\n",
        "\0\0\0binary\n",
    };
    for (size_t i = 0; i < sizeof(kJunk) / sizeof(kJunk[0]); i++) {
        FILE *f = fopen(kPath, "wb");
        CHECK(f != NULL);
        fwrite(kJunk[i], 1, strlen(kJunk[i]), f);
        fclose(f);

        AseSession *loaded = ase_session_load(kPath);
        if (loaded != NULL) {
            /* Whatever came back must be self-consistent. */
            size_t count = ase_session_count(loaded);
            CHECK(ase_session_active(loaded) == 0 || ase_session_active(loaded) < count);
            for (size_t j = 0; j < count; j++) {
                const AseSessionEntry *e = ase_session_entry(loaded, j);
                CHECK(e != NULL && e->path != NULL && e->path[0] != '\0');
            }
            ase_session_destroy(loaded);
        }
    }
    remove(kPath);
}

/* Without the header check the loader would happily parse any file it
 * was pointed at. */
static void test_headerless_file_is_rejected(void) {
    FILE *f = fopen(kPath, "wb");
    CHECK(f != NULL);
    /* A second line, so the loader cannot pass by consuming the first
     * one as a header and then finding nothing. */
    const char *junk = "# some other file entirely\nfile = /home/u/a.c\ncursor = 5\n";
    fwrite(junk, 1, strlen(junk), f);
    fclose(f);
    CHECK(ase_session_load(kPath) == NULL);
    remove(kPath);
}

/* "12abc" is not 12: a half-read number would put the caret somewhere
 * the user never left it. */
static void test_trailing_junk_in_a_number_is_rejected(void) {
    FILE *f = fopen(kPath, "wb");
    CHECK(f != NULL);
    const char *text = "ASE-SESSION 1\nfile = /home/u/a.c\ncursor = 12abc\nscroll = 3x\n";
    fwrite(text, 1, strlen(text), f);
    fclose(f);

    AseSession *loaded = ase_session_load(kPath);
    CHECK(loaded != NULL);
    CHECK(ase_session_count(loaded) == 1);
    const AseSessionEntry *e = ase_session_entry(loaded, 0);
    CHECK(e->cursor == 0);
    CHECK(e->scroll_line == 0);
    ase_session_destroy(loaded);
    remove(kPath);
}

/* An out-of-range active index in the file must not survive the load. */
static void test_out_of_range_active_in_a_file_is_clamped(void) {
    FILE *f = fopen(kPath, "wb");
    CHECK(f != NULL);
    const char *text = "ASE-SESSION 1\nactive = 41\nfile = /home/u/a.c\n";
    fwrite(text, 1, strlen(text), f);
    fclose(f);

    AseSession *loaded = ase_session_load(kPath);
    CHECK(loaded != NULL);
    CHECK(ase_session_count(loaded) == 1);
    CHECK(ase_session_active(loaded) == 0);
    ase_session_destroy(loaded);
    remove(kPath);
}

/* A path with a newline cannot be written without producing a file that
 * reads back as something else entirely, so it is left out. */
static void test_path_with_a_newline_is_not_written(void) {
    AseSession *session = ase_session_create();
    CHECK(ase_session_add(session, "/home/u/be\nfore.c", 1, 1));
    CHECK(ase_session_add(session, "/home/u/after.c", 2, 2));
    CHECK(ase_session_save(session, kPath));
    ase_session_destroy(session);

    AseSession *back = ase_session_load(kPath);
    CHECK(back != NULL);
    /* Only the sound one, and it must not have absorbed the other's
     * cursor or scroll. */
    CHECK(ase_session_count(back) == 1);
    const AseSessionEntry *e = ase_session_entry(back, 0);
    CHECK(strcmp(e->path, "/home/u/after.c") == 0);
    CHECK(e->cursor == 2);
    CHECK(e->scroll_line == 2);
    ase_session_destroy(back);
    remove(kPath);
}

/* An active index past the end would open the wrong tab, or none. */
static void test_active_index_is_clamped(void) {
    AseSession *session = ase_session_create();
    CHECK(ase_session_add(session, "/a", 0, 0));
    ase_session_set_active(session, 7);
    CHECK(ase_session_active(session) == 0);
    ase_session_destroy(session);
}

int main(void) {
    test_round_trip();
    test_order_is_kept();
    test_awkward_paths();
    test_unnamed_buffers_are_skipped();
    test_empty_session_removes_the_file();
    test_missing_file_loads_as_nothing();
    test_malformed_input_is_survivable();
    test_active_index_is_clamped();
    test_headerless_file_is_rejected();
    test_trailing_junk_in_a_number_is_rejected();
    test_out_of_range_active_in_a_file_is_clamped();
    test_path_with_a_newline_is_not_written();

    printf("all session tests passed\n");
    return 0;
}
