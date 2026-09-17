#include "test_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/recovery.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static const char *kDir = "test_recovery_dir";

/* Moves a snapshot's mtime back, so pruning by age is testable without
 * a test that takes a day. */
static void backdate_snapshot(const char *key, long seconds) {
    AseRecoveryList list;
    CHECK(ase_recovery_list(kDir, &list));
    for (size_t i = 0; i < list.count; i++) {
        if (strcmp(list.keys[i], key) != 0) {
            continue;
        }
        struct stat st;
        CHECK(stat(list.paths[i], &st) == 0);
        struct utimbuf times;
        times.actime = st.st_atime - seconds;
        times.modtime = st.st_mtime - seconds;
        CHECK(utime(list.paths[i], &times) == 0);
    }
    ase_recovery_list_free(&list);
}

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

    /* Asked for by key rather than assumed to be the only file there:
     * debris from a failed run used to make this edit the wrong one. */
    char victim[1024] = {0};
    AseRecoveryList list;
    CHECK(ase_recovery_list(kDir, &list));
    for (size_t i = 0; i < list.count; i++) {
        if (strcmp(list.keys[i], "/home/u/real.c") == 0) {
            snprintf(victim, sizeof(victim), "%s", list.paths[i]);
        }
    }
    ase_recovery_list_free(&list);
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

/* An unnamed buffer has no path to key on, which is how it went
 * unsnapshotted entirely. The key is opaque to this layer — it only has
 * to be something no real path collides with. */
static void test_listing_names_every_snapshot(void) {
    CHECK(ase_recovery_write(kDir, "/home/u/one.c", "one\n", 4));
    CHECK(ase_recovery_write(kDir, "/home/u/two.c", "two\n", 4));
    CHECK(ase_recovery_write(kDir, "untitled:900:1", "draft\n", 6));

    AseRecoveryList list;
    CHECK(ase_recovery_list(kDir, &list));
    CHECK(list.count == 3);

    bool saw_one = false, saw_two = false, saw_draft = false;
    for (size_t i = 0; i < list.count; i++) {
        if (strcmp(list.keys[i], "/home/u/one.c") == 0) saw_one = true;
        if (strcmp(list.keys[i], "/home/u/two.c") == 0) saw_two = true;
        if (strcmp(list.keys[i], "untitled:900:1") == 0) saw_draft = true;
    }
    CHECK(saw_one && saw_two && saw_draft);
    ase_recovery_list_free(&list);

    CHECK(ase_recovery_remove(kDir, "/home/u/one.c"));
    CHECK(ase_recovery_remove(kDir, "/home/u/two.c"));
    CHECK(ase_recovery_remove(kDir, "untitled:900:1"));
}

static void test_listing_an_empty_directory(void) {
    AseRecoveryList list;
    CHECK(ase_recovery_list(kDir, &list));
    CHECK(list.count == 0);
    ase_recovery_list_free(&list);
}

/* A file that is not a snapshot, or a truncated one, must not become a
 * phantom entry: the startup prompt offers what this returns. */
static void test_listing_skips_what_is_not_a_snapshot(void) {
    CHECK(ase_recovery_write(kDir, "/home/u/genuine.c", "real\n", 5));

    FILE *f = fopen("test_recovery_dir/junk.recover", "wb");
    CHECK(f != NULL);
    fwrite("not a snapshot", 1, 14, f);
    fclose(f);

    f = fopen("test_recovery_dir/truncated.recover", "wb");
    CHECK(f != NULL);
    fwrite("ASE-RECOVER-1\n99\n", 1, 17, f);
    fclose(f);

    AseRecoveryList list;
    CHECK(ase_recovery_list(kDir, &list));
    CHECK(list.count == 1);
    CHECK(strcmp(list.keys[0], "/home/u/genuine.c") == 0);
    ase_recovery_list_free(&list);

    remove("test_recovery_dir/junk.recover");
    remove("test_recovery_dir/truncated.recover");
    CHECK(ase_recovery_remove(kDir, "/home/u/genuine.c"));
}

/* Snapshots are never pruned today, so one for a file crashed on and
 * never reopened stays for good. */
static void test_pruning_takes_only_the_old(void) {
    CHECK(ase_recovery_write(kDir, "/home/u/fresh.c", "fresh\n", 6));
    CHECK(ase_recovery_write(kDir, "/home/u/stale.c", "stale\n", 6));

    /* Backdate one by two days. */
    AseRecoveryList list;
    CHECK(ase_recovery_list(kDir, &list));
    ase_recovery_list_free(&list);
    backdate_snapshot("/home/u/stale.c", 2 * 24 * 60 * 60);

    CHECK(ase_recovery_prune(kDir, 24 * 60 * 60) == 1);
    CHECK(ase_recovery_exists(kDir, "/home/u/fresh.c"));
    CHECK(!ase_recovery_exists(kDir, "/home/u/stale.c"));

    CHECK(ase_recovery_remove(kDir, "/home/u/fresh.c"));
}

/* Leaving the directory behind makes a second run start from a state
 * the first one did not: a bug in "create the directory" would go
 * unnoticed because it was already there. */
static void remove_test_dirs(void) {
    /* The files first: rmdir on a non-empty directory fails silently,
     * so debris from a failed run used to survive into the next one and
     * break a different test than the one that left it. */
    const char *dirs[] = {"test_recovery_dir/deeper/still", "test_recovery_dir/deeper",
                          "test_recovery_dir"};
    for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
        DIR *dp = opendir(dirs[d]);
        if (dp == NULL) {
            continue;
        }
        struct dirent *entry;
        while ((entry = readdir(dp)) != NULL) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", dirs[d], entry->d_name);
            remove(full);
        }
        closedir(dp);
    }
    for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
        rmdir(dirs[d]);
    }
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
    test_listing_an_empty_directory();
    test_listing_names_every_snapshot();
    test_listing_skips_what_is_not_a_snapshot();
    test_pruning_takes_only_the_old();

    remove_test_dirs();

    printf("all recovery tests passed\n");
    return 0;
}
