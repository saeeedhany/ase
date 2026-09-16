#include "test_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/buffer.h"

#if !defined(_WIN32)
#include <dirent.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

static void test_empty_buffer(void) {
    AseBuffer *buf = ase_buffer_create();
    CHECK(buf != NULL);
    CHECK(ase_buffer_length(buf) == 0);
    CHECK(ase_buffer_line_count(buf) == 1);
    ase_buffer_destroy(buf);
}

static void test_insert_at_start_middle_end(void) {
    AseBuffer *buf = ase_buffer_create();

    CHECK(ase_buffer_insert(buf, 0, "hello", 5));
    expect_content(buf, "hello");

    CHECK(ase_buffer_insert(buf, 5, " world", 6));
    expect_content(buf, "hello world");

    CHECK(ase_buffer_insert(buf, 0, ">> ", 3));
    expect_content(buf, ">> hello world");

    CHECK(ase_buffer_insert(buf, 3, "say ", 4));
    expect_content(buf, ">> say hello world");

    ase_buffer_destroy(buf);
}

static void test_insert_rejects_out_of_range(void) {
    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "abc", 3));
    CHECK(!ase_buffer_insert(buf, 100, "x", 1));
    expect_content(buf, "abc");
    ase_buffer_destroy(buf);
}

static void test_insert_zero_length_is_noop(void) {
    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "abc", 3));
    CHECK(ase_buffer_insert(buf, 1, "", 0));
    expect_content(buf, "abc");
    ase_buffer_destroy(buf);
}

static void test_delete_ranges(void) {
    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "hello world", 11));

    /* delete " world" from the end */
    CHECK(ase_buffer_delete(buf, 5, 6));
    expect_content(buf, "hello");

    CHECK(ase_buffer_insert(buf, 5, ", cruel world", 13));
    expect_content(buf, "hello, cruel world");

    /* delete a middle span that crosses piece boundaries */
    CHECK(ase_buffer_delete(buf, 5, 7)); /* removes ", cruel" */
    expect_content(buf, "hello world");

    /* delete from the very start */
    CHECK(ase_buffer_delete(buf, 0, 6));
    expect_content(buf, "world");

    ase_buffer_destroy(buf);
}

static void test_delete_rejects_out_of_range(void) {
    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "abc", 3));
    CHECK(!ase_buffer_delete(buf, 2, 5));
    CHECK(!ase_buffer_delete(buf, 10, 1));
    expect_content(buf, "abc");
    ase_buffer_destroy(buf);
}

static void test_delete_zero_length_is_noop(void) {
    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "abc", 3));
    CHECK(ase_buffer_delete(buf, 1, 0));
    expect_content(buf, "abc");
    ase_buffer_destroy(buf);
}

static void test_typing_and_backspacing(void) {
    AseBuffer *buf = ase_buffer_create();
    const char *word = "piece table";
    size_t at = 0;

    for (size_t i = 0; i < strlen(word); i++) {
        CHECK(ase_buffer_insert(buf, at, &word[i], 1));
        at++;
    }
    expect_content(buf, "piece table");

    /* backspace the last 6 chars one at a time */
    for (int i = 0; i < 6; i++) {
        size_t len = ase_buffer_length(buf);
        CHECK(ase_buffer_delete(buf, len - 1, 1));
    }
    expect_content(buf, "piece");

    ase_buffer_destroy(buf);
}

static void test_line_count(void) {
    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_line_count(buf) == 1);

    CHECK(ase_buffer_insert(buf, 0, "no newline", 10));
    CHECK(ase_buffer_line_count(buf) == 1);

    CHECK(ase_buffer_insert(buf, ase_buffer_length(buf), "\nline2\nline3", 12));
    CHECK(ase_buffer_line_count(buf) == 3);

    CHECK(ase_buffer_insert(buf, ase_buffer_length(buf), "\n", 1));
    CHECK(ase_buffer_line_count(buf) == 4);

    ase_buffer_destroy(buf);
}

static void test_get_text_partial_ranges(void) {
    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "abc", 3));
    CHECK(ase_buffer_insert(buf, 3, "def", 3));
    CHECK(ase_buffer_insert(buf, 6, "ghi", 3));
    /* three separate pieces spelling "abcdefghi" */

    char out[16];
    size_t copied = ase_buffer_get_text(buf, 2, 5, out); /* spans all 3 pieces */
    CHECK(copied == 5);
    CHECK(memcmp(out, "cdefg", 5) == 0);

    /* request past the end clamps */
    copied = ase_buffer_get_text(buf, 7, 100, out);
    CHECK(copied == 2);
    CHECK(memcmp(out, "hi", 2) == 0);

    ase_buffer_destroy(buf);
}

static void test_file_round_trip(void) {
    const char *path = "test_buffer_roundtrip.tmp";
    const char *content = "line one\nline two\nline three\n";

    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    AseBuffer *buf = ase_buffer_create_from_file(path);
    CHECK(buf != NULL);
    expect_content(buf, content);
    CHECK(ase_buffer_line_count(buf) == 4); /* 3 lines + trailing empty line */

    CHECK(ase_buffer_insert(buf, ase_buffer_length(buf), "line four\n", 10));

    const char *out_path = "test_buffer_roundtrip_out.tmp";
    CHECK(ase_buffer_save_to_file(buf, out_path));

    FILE *check = fopen(out_path, "rb");
    CHECK(check != NULL);
    char read_back[256];
    size_t n = fread(read_back, 1, sizeof(read_back), check);
    fclose(check);

    const char *expected = "line one\nline two\nline three\nline four\n";
    CHECK(n == strlen(expected));
    CHECK(memcmp(read_back, expected, n) == 0);

    remove(path);
    remove(out_path);
    ase_buffer_destroy(buf);
}

/* The pillar in docs/SPEC.md is "never loses user data", and a save that
 * truncates before it writes breaks it: a write that fails partway has
 * already destroyed what was there. Capping the file size makes the
 * failure happen on demand. */
#if !defined(_WIN32)
static void test_failed_save_leaves_the_original_intact(void) {
    const char *path = "test_buffer_atomic.tmp";
    const char *precious = "the user spent all day on this\n";

    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fwrite(precious, 1, strlen(precious), f);
    fclose(f);

    AseBuffer *buf = ase_buffer_create();
    CHECK(buf != NULL);
    char *big = (char *)malloc(200000);
    CHECK(big != NULL);
    memset(big, 'x', 200000);
    CHECK(ase_buffer_insert(buf, 0, big, 200000));
    free(big);

    void (*previous)(int) = signal(SIGXFSZ, SIG_IGN);
    struct rlimit saved;
    getrlimit(RLIMIT_FSIZE, &saved);
    struct rlimit capped;
    capped.rlim_cur = 8192;
    capped.rlim_max = saved.rlim_max;
    CHECK(setrlimit(RLIMIT_FSIZE, &capped) == 0);

    bool saved_ok = ase_buffer_save_to_file(buf, path);

    setrlimit(RLIMIT_FSIZE, &saved);
    signal(SIGXFSZ, previous);

    CHECK(!saved_ok); /* it must report the failure... */

    /* ...and the file must still be what it was. */
    char read_back[256] = {0};
    FILE *check = fopen(path, "rb");
    CHECK(check != NULL);
    size_t n = fread(read_back, 1, sizeof(read_back) - 1, check);
    fclose(check);
    read_back[n] = '\0';
    if (strcmp(read_back, precious) != 0) {
        printf("failed save destroyed the file: %zu bytes left, expected %zu\n", n,
               strlen(precious));
    }
    CHECK(strcmp(read_back, precious) == 0);

    ase_buffer_destroy(buf);
    remove(path);
}

/* A scratch file left next to the user's own is litter, and one left
 * with their content in it is worse. */
static void test_save_leaves_no_scratch_file(void) {
    const char *path = "test_buffer_scratch.tmp";
    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "hello\n", 6));
    CHECK(ase_buffer_save_to_file(buf, path));

    DIR *dir = opendir(".");
    CHECK(dir != NULL);
    int strays = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "test_buffer_scratch") != NULL &&
            strcmp(entry->d_name, path) != 0) {
            printf("left behind: %s\n", entry->d_name);
            strays++;
        }
    }
    closedir(dir);
    CHECK(strays == 0);

    ase_buffer_destroy(buf);
    remove(path);
}

/* A file the user chmodded stays that way; a fresh temp file would
 * otherwise hand it back at whatever the umask says. */
static void test_save_preserves_permissions(void) {
    const char *path = "test_buffer_perm.tmp";
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fwrite("x\n", 1, 2, f);
    fclose(f);
    CHECK(chmod(path, 0600) == 0);

    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "changed\n", 8));
    CHECK(ase_buffer_save_to_file(buf, path));

    struct stat st;
    CHECK(stat(path, &st) == 0);
    if ((st.st_mode & 0777) != 0600) {
        printf("permissions became %o, expected 600\n", st.st_mode & 0777);
    }
    CHECK((st.st_mode & 0777) == 0600);

    ase_buffer_destroy(buf);
    remove(path);
}

/* Saving through a symlink must write what it points at. Replacing the
 * link itself is how an editor silently detaches a dotfile from the
 * repository it is checked into. */
static void test_save_follows_symlinks(void) {
    const char *target = "test_buffer_target.tmp";
    const char *link = "test_buffer_link.tmp";
    remove(link);

    FILE *f = fopen(target, "wb");
    CHECK(f != NULL);
    fwrite("old\n", 1, 4, f);
    fclose(f);
    CHECK(symlink(target, link) == 0);

    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "new\n", 4));
    CHECK(ase_buffer_save_to_file(buf, link));

    struct stat st;
    CHECK(lstat(link, &st) == 0);
    if (!S_ISLNK(st.st_mode)) {
        printf("the symlink was replaced by a regular file\n");
    }
    CHECK(S_ISLNK(st.st_mode));

    char read_back[64] = {0};
    FILE *check = fopen(target, "rb");
    CHECK(check != NULL);
    size_t n = fread(read_back, 1, sizeof(read_back) - 1, check);
    fclose(check);
    read_back[n] = '\0';
    CHECK(strcmp(read_back, "new\n") == 0);

    ase_buffer_destroy(buf);
    remove(link);
    remove(target);
}
#endif

static void test_create_from_missing_file_fails(void) {
    AseBuffer *buf = ase_buffer_create_from_file("this_file_does_not_exist.tmp");
    CHECK(buf == NULL);
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
#if !defined(_WIN32)
    test_failed_save_leaves_the_original_intact();
    test_save_leaves_no_scratch_file();
    test_save_preserves_permissions();
    test_save_follows_symlinks();
#endif

    printf("all buffer tests passed\n");
    return 0;
}
