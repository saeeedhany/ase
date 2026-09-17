#include "ase/recovery.h"

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

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

/* The whole file, or NULL. Sets `*out_len`. */
static char *read_whole_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
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
    if (got != (size_t)size) {
        free(raw);
        return NULL;
    }
    *out_len = got;
    return raw;
}

/* Locates the recorded key inside `raw`. False for anything that is not
 * a whole, well-formed snapshot — a file in this directory that is not
 * one must read as absent rather than as a phantom entry. */
static bool parse_header(const char *raw, size_t got, size_t *key_at, size_t *key_len) {
    if (got < MAGIC_LEN || memcmp(raw, kMagic, MAGIC_LEN) != 0) {
        return false;
    }

    /* <key length>\n immediately after the magic. */
    size_t cursor = MAGIC_LEN;
    size_t len = 0;
    bool any_digit = false;
    while (cursor < got && raw[cursor] >= '0' && raw[cursor] <= '9') {
        len = len * 10 + (size_t)(raw[cursor] - '0');
        any_digit = true;
        cursor++;
        if (len > got) { /* refuses a length that cannot fit */
            return false;
        }
    }
    if (!any_digit || cursor >= got || raw[cursor] != '\n') {
        return false;
    }
    cursor++;

    if (got - cursor < len) {
        return false;
    }
    *key_at = cursor;
    *key_len = len;
    return true;
}

/* Reads the whole snapshot and hands back the content, having checked
 * that it is this file's. Returns NULL for anything unexpected: a
 * snapshot that cannot be trusted is not one to restore from. */
static char *read_snapshot(const char *dir, const char *file_path, size_t *out_len) {
    char *path = snapshot_path(dir, file_path);
    if (path == NULL) {
        return NULL;
    }
    size_t got = 0;
    char *raw = read_whole_file(path, &got);
    free(path);
    if (raw == NULL) {
        return NULL;
    }

    size_t key_at = 0;
    size_t key_len = 0;
    if (!parse_header(raw, got, &key_at, &key_len)) {
        free(raw);
        return NULL;
    }

    /* The name on disk is only a hash; this is what actually decides
     * whether the snapshot belongs to this file. */
    if (key_len != strlen(file_path) || memcmp(raw + key_at, file_path, key_len) != 0) {
        free(raw);
        return NULL;
    }

    size_t cursor = key_at + key_len;
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


/* ---- walking the directory (ADR 0127) ---- */

static const char kSuffix[] = ".recover";
#define SUFFIX_LEN (sizeof(kSuffix) - 1)

static bool is_snapshot_name(const char *name) {
    size_t len = strlen(name);
    return len > SUFFIX_LEN && strcmp(name + len - SUFFIX_LEN, kSuffix) == 0;
}

/* Appends one entry, taking ownership of neither argument. False leaves
 * the list as it was. */
static bool list_append(AseRecoveryList *list, const char *key, size_t key_len, const char *path) {
    char **keys = (char **)realloc(list->keys, (list->count + 1) * sizeof(char *));
    if (keys == NULL) {
        return false;
    }
    list->keys = keys;
    char **paths = (char **)realloc(list->paths, (list->count + 1) * sizeof(char *));
    if (paths == NULL) {
        return false;
    }
    list->paths = paths;

    char *key_copy = (char *)malloc(key_len + 1);
    if (key_copy == NULL) {
        return false;
    }
    memcpy(key_copy, key, key_len);
    key_copy[key_len] = '\0';

    char *path_copy = ase_strdup(path);
    if (path_copy == NULL) {
        free(key_copy);
        return false;
    }

    list->keys[list->count] = key_copy;
    list->paths[list->count] = path_copy;
    list->count++;
    return true;
}

/* Reads one candidate and appends it when it really is a snapshot. */
static void consider(AseRecoveryList *list, const char *full_path) {
    size_t got = 0;
    char *raw = read_whole_file(full_path, &got);
    if (raw == NULL) {
        return;
    }
    size_t key_at = 0;
    size_t key_len = 0;
    if (parse_header(raw, got, &key_at, &key_len)) {
        list_append(list, raw + key_at, key_len, full_path);
    }
    free(raw);
}

bool ase_recovery_list(const char *dir, AseRecoveryList *out) {
    if (dir == NULL || out == NULL) {
        return false;
    }
    out->keys = NULL;
    out->paths = NULL;
    out->count = 0;

#if defined(_WIN32)
    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s\\*%s", dir, kSuffix) >= (int)sizeof(pattern)) {
        return true;
    }
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return true; /* nothing there yet is not a failure */
    }
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        char full_path[MAX_PATH];
        if (snprintf(full_path, sizeof(full_path), "%s\\%s", dir, data.cFileName) >=
            (int)sizeof(full_path)) {
            continue;
        }
        consider(out, full_path);
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        return true; /* nothing there yet is not a failure */
    }
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.' || !is_snapshot_name(entry->d_name)) {
            continue;
        }
        char full_path[4096];
        if (snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name) >=
            (int)sizeof(full_path)) {
            continue;
        }
        consider(out, full_path);
    }
    closedir(dp);
#endif
    return true;
}

void ase_recovery_list_free(AseRecoveryList *list) {
    if (list == NULL) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        free(list->keys[i]);
        free(list->paths[i]);
    }
    free(list->keys);
    free(list->paths);
    list->keys = NULL;
    list->paths = NULL;
    list->count = 0;
}

size_t ase_recovery_prune(const char *dir, long max_age_seconds) {
    AseRecoveryList list;
    if (dir == NULL || max_age_seconds <= 0 || !ase_recovery_list(dir, &list)) {
        return 0;
    }

    time_t now = time(NULL);
    size_t removed = 0;
    for (size_t i = 0; i < list.count; i++) {
        struct stat st;
        if (stat(list.paths[i], &st) != 0) {
            continue;
        }
        /* A clock that moved backwards would otherwise delete
         * everything: a snapshot from the future is not an old one. */
        double age = difftime(now, st.st_mtime);
        if (age > (double)max_age_seconds && remove(list.paths[i]) == 0) {
            removed++;
        }
    }

    ase_recovery_list_free(&list);
    return removed;
}
