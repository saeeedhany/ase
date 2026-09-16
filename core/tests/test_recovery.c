#include "test_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/recovery.h"

#include <dirent.h>
#include <unistd.h>

static const char *kDir = "test_recovery_dir";

static void expect_roundtrip(const char *label, const char *path, const char *content, size_t len) {
    CHECK(ase_recovery_write(kDir, path, content, len));
    CHECK(ase_recovery_exists(kDir, path));

    size_t out_len = 0;
    char *got = ase_recovery_read(kDir, path, &out_len);
    if (got == NULL) {
        printf("%s: nothing read back\n", label);
        CHECK(0);
        return;
    }
    if (out_len != len || memcmp(got, content, len) != 0) {
        printf("%s: read back %zu bytes, wrote %zu\n", label, out_len, len);
        CHECK(0);
    }
    free(got);
    CHECK(ase_recovery_remove(kDir, path));
    CHECK(!ase_recovery_exists(kDir, path));
}

static void test_roundtrip(void) {
    expect_roundtrip("plain", "/home/u/file.c", "int main(void) {}\n", 18);
}

/* A buffer is bytes, not a string: an editor that mangles a NUL or a
 * stray newline on recovery has lost data just as surely. */
static void test_binary_safe(void) {
    const char content[] = {'a', '\0', '\n', 'b', '\r', '\0', 'z'};
    expect_roundtrip("binary", "/home/u/bin.dat", content, sizeof(content));
}

static void test_empty_content(void) {
    expect_roundtrip("empty", "/home/u/empty.txt", "", 0);
}

/* Paths are not identifiers: they carry spaces, dots and, on Linux,
 * newlines. The name on disk is a hash, so none of it should matter. */
static void test_awkward_paths(void) {
    expect_roundtrip("spaces", "/home/u/my documents/a file.txt", "x\n", 2);
    expect_roundtrip("newline", "/home/u/we\nird.txt", "y\n", 2);
    expect_roundtrip("dots", "/home/u/../u/./f.c", "z\n", 2);
}

static void test_missing_reads_as_nothing(void) {
    size_t len = 12345;
    CHECK(!ase_recovery_exists(kDir, "/home/u/never-written.c"));
    CHECK(ase_recovery_read(kDir, "/home/u/never-written.c", &len) == NULL);
    /* Removing what is not there is not a failure. */
    CHECK(ase_recovery_remove(kDir, "/home/u/never-written.c"));
}

/* Two files must not share a snapshot, and one must never read back as
 * the other. */
static void test_distinct_files_are_distinct(void) {
    CHECK(ase_recovery_write(kDir, "/home/u/one.c", "first\n", 6));
    CHECK(ase_recovery_write(kDir, "/home/u/two.c", "second\n", 7));

    size_t len = 0;
    char *one = ase_recovery_read(kDir, "/home/u/one.c", &len);
    CHECK(one != NULL && len == 6 && memcmp(one, "first\n", 6) == 0);
    free(one);

    char *two = ase_recovery_read(kDir, "/home/u/two.c", &len);
    CHECK(two != NULL && len == 7 && memcmp(two, "second\n", 7) == 0);
    free(two);

    /* Dropping one leaves the other. */
    CHECK(ase_recovery_remove(kDir, "/home/u/one.c"));
    CHECK(!ase_recovery_exists(kDir, "/home/u/one.c"));
    CHECK(ase_recovery_exists(kDir, "/home/u/two.c"));
    CHECK(ase_recovery_remove(kDir, "/home/u/two.c"));
}

/* Overwriting is the common case: the snapshot is rewritten every time
 * typing pauses. */
static void test_rewrite_replaces(void) {
    CHECK(ase_recovery_write(kDir, "/home/u/x.c", "old\n", 4));
    CHECK(ase_recovery_write(kDir, "/home/u/x.c", "new content\n", 12));
    size_t len = 0;
    char *got = ase_recovery_read(kDir, "/home/u/x.c", &len);
    CHECK(got != NULL && len == 12 && memcmp(got, "new content\n", 12) == 0);
    free(got);
    CHECK(ase_recovery_remove(kDir, "/home/u/x.c"));
}

/* The name on disk is a hash, so two paths could in principle collide.
 * The path is recorded inside the snapshot and checked on read, so a
 * collision reads as "no recovery" rather than as someone else's text.
 * Simulated by rewriting the recorded path under the editor's feet. */
static void test_snapshot_naming_a_different_file_is_ignored(void) {
    CHECK(ase_recovery_write(kDir, "/home/u/real.c", "contents\n", 9));

    /* Only one snapshot is in the directory, so it is the one to edit. */
    char victim[1024] = {0};
    DIR *dir = opendir(kDir);
    CHECK(dir != NULL);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".recover") != NULL) {
            snprintf(victim, sizeof(victim), "%s/%s", kDir, entry->d_name);
            break;
        }
    }
    closedir(dir);
    CHECK(victim[0] != '\0');

    /* Same length so only the bytes change, not the layout. */
    FILE *f = fopen(victim, "r+b");
    CHECK(f != NULL);
    char head[256];
    size_t n = fread(head, 1, sizeof(head), f);
    char *found = (char *)memmem(head, n, "/home/u/real.c", 14);
    CHECK(found != NULL);
    memcpy(found, "/home/u/FAKE.c", 14);
    fseek(f, 0, SEEK_SET);
    fwrite(head, 1, n, f);
    fclose(f);

    size_t len = 12345;
    CHECK(ase_recovery_read(kDir, "/home/u/real.c", &len) == NULL);
    CHECK(!ase_recovery_exists(kDir, "/home/u/real.c"));

    remove(victim);
}

static void test_creates_its_directory(void) {
    const char *nested = "test_recovery_dir/deeper/still";
    CHECK(ase_recovery_write(nested, "/home/u/n.c", "hi\n", 3));
    CHECK(ase_recovery_exists(nested, "/home/u/n.c"));
    CHECK(ase_recovery_remove(nested, "/home/u/n.c"));
}

/* Leaving the directory behind makes a second run start from a state
 * the first one did not: a bug in "create the directory" would go
 * unnoticed because it was already there. */
static void remove_test_dirs(void) {
    rmdir("test_recovery_dir/deeper/still");
    rmdir("test_recovery_dir/deeper");
    rmdir("test_recovery_dir");
}

int main(void) {
    remove_test_dirs();

    test_roundtrip();
    test_binary_safe();
    test_empty_content();
    test_awkward_paths();
    test_missing_reads_as_nothing();
    test_distinct_files_are_distinct();
    test_rewrite_replaces();
    test_snapshot_naming_a_different_file_is_ignored();
    test_creates_its_directory();

    remove_test_dirs();

    printf("all recovery tests passed\n");
    return 0;
}
