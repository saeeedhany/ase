#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/buffer.h"

static void expect_content(AseBuffer *buf, const char *expected) {
    size_t len = strlen(expected);
    assert(ase_buffer_length(buf) == len);

    char *actual = (char *)malloc(len + 1);
    assert(actual != NULL);
    size_t copied = ase_buffer_get_text(buf, 0, len, actual);
    actual[len] = '\0';

    assert(copied == len);
    assert(memcmp(actual, expected, len) == 0);
    free(actual);
}

static void test_empty_buffer(void) {
    AseBuffer *buf = ase_buffer_create();
    assert(buf != NULL);
    assert(ase_buffer_length(buf) == 0);
    assert(ase_buffer_line_count(buf) == 1);
    ase_buffer_destroy(buf);
}

static void test_insert_at_start_middle_end(void) {
    AseBuffer *buf = ase_buffer_create();

    assert(ase_buffer_insert(buf, 0, "hello", 5));
    expect_content(buf, "hello");

    assert(ase_buffer_insert(buf, 5, " world", 6));
    expect_content(buf, "hello world");

    assert(ase_buffer_insert(buf, 0, ">> ", 3));
    expect_content(buf, ">> hello world");

    assert(ase_buffer_insert(buf, 3, "say ", 4));
    expect_content(buf, ">> say hello world");

    ase_buffer_destroy(buf);
}

static void test_insert_rejects_out_of_range(void) {
    AseBuffer *buf = ase_buffer_create();
    assert(ase_buffer_insert(buf, 0, "abc", 3));
    assert(!ase_buffer_insert(buf, 100, "x", 1));
    expect_content(buf, "abc");
    ase_buffer_destroy(buf);
}

static void test_insert_zero_length_is_noop(void) {
    AseBuffer *buf = ase_buffer_create();
    assert(ase_buffer_insert(buf, 0, "abc", 3));
    assert(ase_buffer_insert(buf, 1, "", 0));
    expect_content(buf, "abc");
    ase_buffer_destroy(buf);
}

static void test_delete_ranges(void) {
    AseBuffer *buf = ase_buffer_create();
    assert(ase_buffer_insert(buf, 0, "hello world", 11));

    /* delete " world" from the end */
    assert(ase_buffer_delete(buf, 5, 6));
    expect_content(buf, "hello");

    assert(ase_buffer_insert(buf, 5, ", cruel world", 13));
    expect_content(buf, "hello, cruel world");

    /* delete a middle span that crosses piece boundaries */
    assert(ase_buffer_delete(buf, 5, 7)); /* removes ", cruel" */
    expect_content(buf, "hello world");

    /* delete from the very start */
    assert(ase_buffer_delete(buf, 0, 6));
    expect_content(buf, "world");

    ase_buffer_destroy(buf);
}

static void test_delete_rejects_out_of_range(void) {
    AseBuffer *buf = ase_buffer_create();
    assert(ase_buffer_insert(buf, 0, "abc", 3));
    assert(!ase_buffer_delete(buf, 2, 5));
    assert(!ase_buffer_delete(buf, 10, 1));
    expect_content(buf, "abc");
    ase_buffer_destroy(buf);
}

static void test_delete_zero_length_is_noop(void) {
    AseBuffer *buf = ase_buffer_create();
    assert(ase_buffer_insert(buf, 0, "abc", 3));
    assert(ase_buffer_delete(buf, 1, 0));
    expect_content(buf, "abc");
    ase_buffer_destroy(buf);
}

static void test_typing_and_backspacing(void) {
    AseBuffer *buf = ase_buffer_create();
    const char *word = "piece table";
    size_t at = 0;

    for (size_t i = 0; i < strlen(word); i++) {
        assert(ase_buffer_insert(buf, at, &word[i], 1));
        at++;
    }
    expect_content(buf, "piece table");

    /* backspace the last 6 chars one at a time */
    for (int i = 0; i < 6; i++) {
        size_t len = ase_buffer_length(buf);
        assert(ase_buffer_delete(buf, len - 1, 1));
    }
    expect_content(buf, "piece");

    ase_buffer_destroy(buf);
}

static void test_line_count(void) {
    AseBuffer *buf = ase_buffer_create();
    assert(ase_buffer_line_count(buf) == 1);

    assert(ase_buffer_insert(buf, 0, "no newline", 10));
    assert(ase_buffer_line_count(buf) == 1);

    assert(ase_buffer_insert(buf, ase_buffer_length(buf), "\nline2\nline3", 12));
    assert(ase_buffer_line_count(buf) == 3);

    assert(ase_buffer_insert(buf, ase_buffer_length(buf), "\n", 1));
    assert(ase_buffer_line_count(buf) == 4);

    ase_buffer_destroy(buf);
}

static void test_get_text_partial_ranges(void) {
    AseBuffer *buf = ase_buffer_create();
    assert(ase_buffer_insert(buf, 0, "abc", 3));
    assert(ase_buffer_insert(buf, 3, "def", 3));
    assert(ase_buffer_insert(buf, 6, "ghi", 3));
    /* three separate pieces spelling "abcdefghi" */

    char out[16];
    size_t copied = ase_buffer_get_text(buf, 2, 5, out); /* spans all 3 pieces */
    assert(copied == 5);
    assert(memcmp(out, "cdefg", 5) == 0);

    /* request past the end clamps */
    copied = ase_buffer_get_text(buf, 7, 100, out);
    assert(copied == 2);
    assert(memcmp(out, "hi", 2) == 0);

    ase_buffer_destroy(buf);
}

static void test_file_round_trip(void) {
    const char *path = "test_buffer_roundtrip.tmp";
    const char *content = "line one\nline two\nline three\n";

    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    AseBuffer *buf = ase_buffer_create_from_file(path);
    assert(buf != NULL);
    expect_content(buf, content);
    assert(ase_buffer_line_count(buf) == 4); /* 3 lines + trailing empty line */

    assert(ase_buffer_insert(buf, ase_buffer_length(buf), "line four\n", 10));

    const char *out_path = "test_buffer_roundtrip_out.tmp";
    assert(ase_buffer_save_to_file(buf, out_path));

    FILE *check = fopen(out_path, "rb");
    assert(check != NULL);
    char read_back[256];
    size_t n = fread(read_back, 1, sizeof(read_back), check);
    fclose(check);

    const char *expected = "line one\nline two\nline three\nline four\n";
    assert(n == strlen(expected));
    assert(memcmp(read_back, expected, n) == 0);

    remove(path);
    remove(out_path);
    ase_buffer_destroy(buf);
}

static void test_create_from_missing_file_fails(void) {
    AseBuffer *buf = ase_buffer_create_from_file("this_file_does_not_exist.tmp");
    assert(buf == NULL);
}

int main(void) {
    test_empty_buffer();
    test_insert_at_start_middle_end();
    test_insert_rejects_out_of_range();
    test_insert_zero_length_is_noop();
    test_delete_ranges();
    test_delete_rejects_out_of_range();
    test_delete_zero_length_is_noop();
    test_typing_and_backspacing();
    test_line_count();
    test_get_text_partial_ranges();
    test_file_round_trip();
    test_create_from_missing_file_fails();

    printf("all buffer tests passed\n");
    return 0;
}
