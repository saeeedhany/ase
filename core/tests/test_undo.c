#include "test_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/buffer.h"
#include "ase/undo.h"

static void expect_content(AseBuffer *buf, const char *expected) {
    size_t len = strlen(expected);
    CHECK(ase_buffer_length(buf) == len);

    char *actual = (char *)malloc(len + 1);
    CHECK(actual != NULL);
    size_t copied = ase_buffer_get_text(buf, 0, len, actual);
    actual[len] = '\0';

    CHECK(copied == len);
    CHECK(memcmp(actual, expected, len) == 0);
    free(actual);
}

/* Mimics EditorViewport::insertTextAt: insert, record, advance cursor. */
static void do_insert(AseBuffer *buf, AseUndoStack *undo, size_t *cursor, const char *text, size_t len) {
    if (ase_buffer_insert(buf, *cursor, text, len)) {
        ase_undo_record_insert(undo, *cursor, text, len);
        *cursor += len;
    }
}

/* Mimics EditorViewport::deleteBackwardAt: snapshot, delete, record. */
static void do_backspace(AseBuffer *buf, AseUndoStack *undo, size_t *cursor) {
    if (*cursor == 0) {
        return;
    }
    size_t start = *cursor - 1;
    char removed;
    ase_buffer_get_text(buf, start, 1, &removed);
    if (ase_buffer_delete(buf, start, 1)) {
        ase_undo_record_delete(undo, start, &removed, 1);
        *cursor = start;
    }
}

static void test_single_insert_undo_redo(void) {
    AseBuffer *buf = ase_buffer_create();
    AseUndoStack *undo = ase_undo_create();
    size_t cursor = 0;

    ase_undo_begin_group(undo, &cursor, 1);
    do_insert(buf, undo, &cursor, "hi", 2);
    ase_undo_end_group(undo, &cursor, 1);
    expect_content(buf, "hi");
    CHECK(cursor == 2);

    size_t *cursors = NULL;
    size_t count = 0;
    CHECK(ase_undo_undo(undo, buf, &cursors, &count));
    expect_content(buf, "");
    CHECK(count == 1 && cursors[0] == 0);
    free(cursors);

    CHECK(ase_undo_redo(undo, buf, &cursors, &count));
    expect_content(buf, "hi");
    CHECK(count == 1 && cursors[0] == 2);
    free(cursors);

    ase_undo_destroy(undo);
    ase_buffer_destroy(buf);
}

static void test_single_delete_undo_redo(void) {
    AseBuffer *buf = ase_buffer_create();
    AseUndoStack *undo = ase_undo_create();
    size_t cursor = 0;

    ase_undo_begin_group(undo, &cursor, 1);
    do_insert(buf, undo, &cursor, "abc", 3);
    ase_undo_end_group(undo, &cursor, 1);

    ase_undo_begin_group(undo, &cursor, 1);
    do_backspace(buf, undo, &cursor);
    ase_undo_end_group(undo, &cursor, 1);
    expect_content(buf, "ab");
    CHECK(cursor == 2);

    size_t *cursors = NULL;
    size_t count = 0;
    CHECK(ase_undo_undo(undo, buf, &cursors, &count));
    expect_content(buf, "abc");
    CHECK(count == 1 && cursors[0] == 3);
    free(cursors);

    CHECK(ase_undo_redo(undo, buf, &cursors, &count));
    expect_content(buf, "ab");
    CHECK(count == 1 && cursors[0] == 2);
    free(cursors);

    ase_undo_destroy(undo);
    ase_buffer_destroy(buf);
}

/* Grouped multi-cursor edit: two cursors typing "X" at once, processed
 * highest-offset-first (mirroring EditorViewport's actual loop order),
 * must undo as a single action reverting both. */
static void test_grouped_multi_cursor_undo(void) {
    AseBuffer *buf = ase_buffer_create();
    AseUndoStack *undo = ase_undo_create();
    CHECK(ase_buffer_insert(buf, 0, "aabb", 4));

    size_t cursors_before[2] = {2, 4}; /* between the a's-and-b's, and at the end */
    ase_undo_begin_group(undo, cursors_before, 2);
    /* highest offset first, per the multi-cursor discipline */
    size_t c1 = 4;
    do_insert(buf, undo, &c1, "Y", 1);
    size_t c0 = 2;
    do_insert(buf, undo, &c0, "X", 1);
    size_t cursors_after[2] = {c0, c1};
    ase_undo_end_group(undo, cursors_after, 2);

    expect_content(buf, "aaXbbY");

    size_t *cursors = NULL;
    size_t count = 0;
    CHECK(ase_undo_undo(undo, buf, &cursors, &count));
    expect_content(buf, "aabb");
    CHECK(count == 2 && cursors[0] == 2 && cursors[1] == 4);
    free(cursors);

    CHECK(ase_undo_redo(undo, buf, &cursors, &count));
    expect_content(buf, "aaXbbY");
    CHECK(count == 2);
    free(cursors);

    ase_undo_destroy(undo);
    ase_buffer_destroy(buf);
}

static void test_redo_cleared_by_new_edit(void) {
    AseBuffer *buf = ase_buffer_create();
    AseUndoStack *undo = ase_undo_create();
    size_t cursor = 0;

    ase_undo_begin_group(undo, &cursor, 1);
    do_insert(buf, undo, &cursor, "a", 1);
    ase_undo_end_group(undo, &cursor, 1);

    size_t *cursors = NULL;
    size_t count = 0;
    CHECK(ase_undo_undo(undo, buf, &cursors, &count));
    CHECK(count == 1);
    cursor = cursors[0];
    free(cursors);
    expect_content(buf, "");

    /* A fresh edit should invalidate the pending redo. */
    ase_undo_begin_group(undo, &cursor, 1);
    do_insert(buf, undo, &cursor, "b", 1);
    ase_undo_end_group(undo, &cursor, 1);
    expect_content(buf, "b");

    CHECK(!ase_undo_redo(undo, buf, &cursors, &count));

    ase_undo_destroy(undo);
    ase_buffer_destroy(buf);
}

static void test_noop_group_is_not_pushed(void) {
    AseBuffer *buf = ase_buffer_create();
    AseUndoStack *undo = ase_undo_create();
    size_t cursor = 0;

    /* Backspace at offset 0 is a no-op -- nothing recorded. */
    ase_undo_begin_group(undo, &cursor, 1);
    do_backspace(buf, undo, &cursor);
    ase_undo_end_group(undo, &cursor, 1);

    size_t *cursors = NULL;
    size_t count = 0;
    CHECK(!ase_undo_undo(undo, buf, &cursors, &count));

    ase_undo_destroy(undo);
    ase_buffer_destroy(buf);
}

static void test_undo_past_beginning_is_noop(void) {
    AseBuffer *buf = ase_buffer_create();
    AseUndoStack *undo = ase_undo_create();
    size_t cursor = 0;

    ase_undo_begin_group(undo, &cursor, 1);
    do_insert(buf, undo, &cursor, "z", 1);
    ase_undo_end_group(undo, &cursor, 1);

    size_t *cursors = NULL;
    size_t count = 0;
    CHECK(ase_undo_undo(undo, buf, &cursors, &count));
    free(cursors);

    /* Nothing left to undo -- must not crash, must report false. */
    CHECK(!ase_undo_undo(undo, buf, &cursors, &count));
    expect_content(buf, "");

    ase_undo_destroy(undo);
    ase_buffer_destroy(buf);
}


/*
 * The whole point of the state id: it answers "is the buffer what it
 * was when I last looked?", which a set-on-edit flag cannot, because an
 * edit and its undo cancel out.
 */
static void test_state_id_returns_after_undo(void) {
    AseBuffer *buf = ase_buffer_create();
    AseUndoStack *undo = ase_undo_create();
    size_t cursor = 0;

    size_t saved = ase_undo_state_id(undo); /* pretend a save here */

    ase_undo_begin_group(undo, &cursor, 1);
    do_insert(buf, undo, &cursor, " ", 1);
    ase_undo_end_group(undo, &cursor, 1);
    CHECK(ase_undo_state_id(undo) != saved);

    size_t *cursors = NULL;
    size_t count = 0;
    CHECK(ase_undo_undo(undo, buf, &cursors, &count));
    free(cursors);
    /* Back to the saved content -- so back to the saved id. */
    CHECK(ase_undo_state_id(undo) == saved);

    CHECK(ase_undo_redo(undo, buf, &cursors, &count));
    free(cursors);
    CHECK(ase_undo_state_id(undo) != saved);

    ase_undo_destroy(undo);
    ase_buffer_destroy(buf);
}

/*
 * Undo one group, commit a different one, and a *count* of groups is
 * back where it started while the content is not. The id must not be.
 */
static void test_state_id_is_not_a_group_count(void) {
    AseBuffer *buf = ase_buffer_create();
    AseUndoStack *undo = ase_undo_create();
    size_t cursor = 0;

    ase_undo_begin_group(undo, &cursor, 1);
    do_insert(buf, undo, &cursor, "a", 1);
    ase_undo_end_group(undo, &cursor, 1);
    size_t after_first = ase_undo_state_id(undo);

    size_t *cursors = NULL;
    size_t count = 0;
    CHECK(ase_undo_undo(undo, buf, &cursors, &count));
    free(cursors);

    cursor = 0;
    ase_undo_begin_group(undo, &cursor, 1);
    do_insert(buf, undo, &cursor, "b", 1);
    ase_undo_end_group(undo, &cursor, 1);

    expect_content(buf, "b");
    CHECK(ase_undo_state_id(undo) != after_first);

    ase_undo_destroy(undo);
    ase_buffer_destroy(buf);
}

/* A group that recorded nothing is not a state change. */
static void test_state_id_unchanged_by_noop_group(void) {
    AseUndoStack *undo = ase_undo_create();
    size_t cursor = 0;

    size_t before = ase_undo_state_id(undo);
    ase_undo_begin_group(undo, &cursor, 1);
    ase_undo_end_group(undo, &cursor, 1);
    CHECK(ase_undo_state_id(undo) == before);

    ase_undo_destroy(undo);
}

int main(void) {
    test_single_insert_undo_redo();
    test_single_delete_undo_redo();
    test_grouped_multi_cursor_undo();
    test_redo_cleared_by_new_edit();
    test_noop_group_is_not_pushed();
    test_undo_past_beginning_is_noop();
    test_state_id_returns_after_undo();
    test_state_id_is_not_a_group_count();
    test_state_id_unchanged_by_noop_group();

    printf("all undo tests passed\n");
    return 0;
}
