#ifndef ASE_INTERNAL_H
#define ASE_INTERNAL_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* Plain ISO C90 strdup — avoids relying on POSIX strdup/MSVC's _strdup.
 * Internal to core: not part of any public header. */
static inline char *ase_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len);
    return copy;
}

/* malloc + memcpy of a known-length, not-necessarily-terminated run. */
static inline char *ase_memdup(const char *text, size_t len) {
    char *copy = (char *)malloc(len > 0 ? len : 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len);
    return copy;
}

/* ---- the write-then-rename save, per platform (see docs/adr/0109) ---- */

/* The path with symlinks resolved, or NULL when it cannot be resolved —
 * which includes a file that does not exist yet, where the caller should
 * use the path as given. Caller frees. */
static inline char *ase_real_path(const char *path) {
#if defined(_WIN32)
    char *out = _fullpath(NULL, path, 0);
    return out; /* _fullpath allocates with malloc */
#else
    return realpath(path, NULL);
#endif
}

/* A scratch name in the same directory as `target`: rename() is atomic
 * only within one filesystem, so /tmp is not an option. Dot-prefixed so
 * it stays out of the way if anything ever leaves one behind. */
static inline char *ase_temp_path_beside(const char *target) {
    const char *slash = strrchr(target, '/');
#if defined(_WIN32)
    const char *backslash = strrchr(target, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
#endif
    size_t dir_len = (slash != NULL) ? (size_t)(slash - target) + 1 : 0;
    const char *name = target + dir_len;

    /* <dir>/.<name>.ase-<pid>.tmp */
    size_t size = dir_len + strlen(name) + 32;
    char *tmp = (char *)malloc(size);
    if (tmp == NULL) {
        return NULL;
    }
    memcpy(tmp, target, dir_len);
#if defined(_WIN32)
    snprintf(tmp + dir_len, size - dir_len, ".%s.ase-%lu.tmp", name,
             (unsigned long)GetCurrentProcessId());
#else
    snprintf(tmp + dir_len, size - dir_len, ".%s.ase-%ld.tmp", name, (long)getpid());
#endif
    return tmp;
}

/* Creates the scratch file readable only by its owner. The real
 * permissions are copied on just before the rename, so the user's
 * content is never briefly visible to anyone who could not already see
 * the original. */
static inline FILE *ase_open_private(const char *path) {
#if defined(_WIN32)
    return fopen(path, "wb");
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return NULL;
    }
    FILE *f = fdopen(fd, "wb");
    if (f == NULL) {
        close(fd);
        remove(path);
    }
    return f;
#endif
}

/* Pushes the stream's bytes to the device, not just to the OS cache.
 * Without it the rename can land before the contents do, and a power
 * loss leaves an intact name over an empty file. */
static inline bool ase_fsync_stream(FILE *f) {
#if defined(_WIN32)
    return _commit(_fileno(f)) == 0;
#else
    return fsync(fileno(f)) == 0;
#endif
}

static inline void ase_copy_permissions(const char *from, const char *to) {
#if defined(_WIN32)
    (void)from;
    (void)to;
#else
    struct stat st;
    if (stat(from, &st) == 0) {
        chmod(to, st.st_mode & 07777);
    } else {
        /* A new file: whatever the umask allows, as fopen would have. */
        mode_t mask = umask(0);
        umask(mask);
        chmod(to, 0666 & ~mask);
    }
#endif
}

/* POSIX rename() replaces the destination atomically. The Win32 one does
 * not, so it needs MoveFileEx. */
static inline bool ase_rename_over(const char *from, const char *to) {
#if defined(_WIN32)
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(from, to) == 0;
#endif
}

/* A rename is only as durable as the directory entry recording it. Best
 * effort: a filesystem that refuses this is not one to fail a save over. */
static inline void ase_fsync_parent_directory(const char *path) {
#if defined(_WIN32)
    (void)path;
#else
    char *copy = ase_strdup(path);
    if (copy == NULL) {
        return;
    }
    int fd = open(dirname(copy), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
    free(copy);
#endif
}

#endif
