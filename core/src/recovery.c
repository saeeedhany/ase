#include "ase/recovery.h"

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Format, so a snapshot can be read by eye or by another tool:
 *
 *   ASE-RECOVER-1\n
 *   <length of the path, in decimal>\n
 *   <path bytes>
 *   <content bytes>
 *
 * The length prefix rather than a delimiter because a path may contain
 * anything a filesystem allows, newlines included, and the content is
 * bytes rather than text. Everything after the path is content, so its
 * length never needs recording.
 */
static const char kMagic[] = "ASE-RECOVER-1\n";
#define MAGIC_LEN (sizeof(kMagic) - 1)

/* FNV-1a. Not a security boundary — the recorded path is what decides
 * whether a snapshot belongs to a file; this only has to spread names
 * across the directory. */
static unsigned long long hash_path(const char *path) {
    unsigned long long h = 14695981039346656037ULL;
    for (const unsigned char *p = (const unsigned char *)path; *p != '\0'; p++) {
        h ^= (unsigned long long)*p;
        h *= 1099511628211ULL;
    }
    return h;
}

static char *snapshot_path(const char *dir, const char *file_path) {
    if (dir == NULL || file_path == NULL) {
        return NULL;
    }
    size_t size = strlen(dir) + 32;
    char *out = (char *)malloc(size);
    if (out == NULL) {
        return NULL;
    }
    snprintf(out, size, "%s/%016llx.recover", dir, hash_path(file_path));
    return out;
}

typedef struct {
    const char *file_path;
    const char *content;
    size_t len;
} Snapshot;

static bool write_snapshot(FILE *f, void *user) {
    const Snapshot *s = (const Snapshot *)user;
    size_t path_len = strlen(s->file_path);
    if (fwrite(kMagic, 1, MAGIC_LEN, f) != MAGIC_LEN) {
        return false;
    }
    if (fprintf(f, "%zu\n", path_len) < 0) {
        return false;
    }
    if (fwrite(s->file_path, 1, path_len, f) != path_len) {
        return false;
    }
    return s->len == 0 || fwrite(s->content, 1, s->len, f) == s->len;
}

bool ase_recovery_write(const char *dir, const char *file_path, const char *content, size_t len) {
    if (dir == NULL || file_path == NULL || (content == NULL && len > 0)) {
        return false;
    }
    char *path = snapshot_path(dir, file_path);
    if (path == NULL) {
        return false;
    }
    /* The directory may not exist yet — first unsaved edit of a fresh
     * install reaches here before anything else has written to it. */
    ase_make_parent_dirs(path);

    Snapshot snapshot = {file_path, content, len};
    bool ok = ase_write_atomically(path, write_snapshot, &snapshot);
    free(path);
    return ok;
}

/* Reads the whole snapshot and hands back the content, having checked
 * that it is this file's. Returns NULL for anything unexpected: a
 * snapshot that cannot be trusted is not one to restore from. */
static char *read_snapshot(const char *dir, const char *file_path, size_t *out_len) {
    char *path = snapshot_path(dir, file_path);
    if (path == NULL) {
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    free(path);
    if (f == NULL) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < (long)MAGIC_LEN || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *raw = (char *)malloc((size_t)size);
    if (raw == NULL) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(raw, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size || memcmp(raw, kMagic, MAGIC_LEN) != 0) {
        free(raw);
        return NULL;
    }

    /* <path length>\n immediately after the magic. */
    size_t cursor = MAGIC_LEN;
    size_t path_len = 0;
    bool any_digit = false;
    while (cursor < got && raw[cursor] >= '0' && raw[cursor] <= '9') {
        path_len = path_len * 10 + (size_t)(raw[cursor] - '0');
        any_digit = true;
        cursor++;
        if (path_len > got) { /* refuses a length that cannot fit */
            free(raw);
            return NULL;
        }
    }
    if (!any_digit || cursor >= got || raw[cursor] != '\n') {
        free(raw);
        return NULL;
    }
    cursor++;

    if (got - cursor < path_len) {
        free(raw);
        return NULL;
    }
    /* The name on disk is only a hash; this is what actually decides
     * whether the snapshot belongs to this file. */
    if (path_len != strlen(file_path) || memcmp(raw + cursor, file_path, path_len) != 0) {
        free(raw);
        return NULL;
    }
    cursor += path_len;

    size_t content_len = got - cursor;
    char *content = ase_memdup(raw + cursor, content_len);
    free(raw);
    if (content == NULL) {
        return NULL;
    }
    if (out_len != NULL) {
        *out_len = content_len;
    }
    return content;
}

char *ase_recovery_read(const char *dir, const char *file_path, size_t *out_len) {
    if (dir == NULL || file_path == NULL) {
        return NULL;
    }
    return read_snapshot(dir, file_path, out_len);
}

bool ase_recovery_exists(const char *dir, const char *file_path) {
    size_t len = 0;
    char *content = ase_recovery_read(dir, file_path, &len);
    if (content == NULL) {
        return false;
    }
    free(content);
    return true;
}

bool ase_recovery_remove(const char *dir, const char *file_path) {
    char *path = snapshot_path(dir, file_path);
    if (path == NULL) {
        return false;
    }
    remove(path); /* absent is not a failure: the caller wanted it gone */
    bool gone = fopen(path, "rb") == NULL;
    free(path);
    return gone;
}
