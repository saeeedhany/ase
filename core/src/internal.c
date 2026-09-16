#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

char *ase_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len);
    return copy;
}

char *ase_memdup(const char *text, size_t len) {
    char *copy = (char *)malloc(len > 0 ? len : 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len);
    return copy;
}

/* The last '/' — or on Windows, whichever separator comes last. */
static const char *last_separator(const char *path) {
    const char *slash = strrchr(path, '/');
#if defined(_WIN32)
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        return backslash;
    }
#endif
    return slash;
}

static bool make_dir(const char *dir) {
#if defined(_WIN32)
    return _mkdir(dir) == 0 || errno == EEXIST;
#else
    return mkdir(dir, 0755) == 0 || errno == EEXIST;
#endif
}

void ase_make_parent_dirs(const char *path) {
    const char *last = last_separator(path);
    if (last == NULL) {
        return;
    }
    char *dir = ase_memdup(path, (size_t)(last - path) + 1);
    if (dir == NULL) {
        return;
    }
    dir[last - path] = '\0';

    /* Each component in turn, so a nested directory works the first time. */
    for (char *p = dir + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char sep = *p;
            *p = '\0';
            make_dir(dir);
            *p = sep;
        }
    }
    make_dir(dir);
    free(dir);
}

static char *real_path(const char *path) {
#if defined(_WIN32)
    return _fullpath(NULL, path, 0);
#else
    return realpath(path, NULL);
#endif
}

/* Beside the target, because rename() is atomic only within one
 * filesystem and /tmp is routinely a different one. */
static char *temp_path_beside(const char *target) {
    const char *last = last_separator(target);
    size_t dir_len = (last != NULL) ? (size_t)(last - target) + 1 : 0;
    const char *name = target + dir_len;

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

/* 0600 until the real permissions go on just before the rename, so the
 * content is never briefly readable by anyone who could not already
 * read the original. */
static FILE *open_private(const char *path) {
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

/* To the device, not just the OS cache: otherwise the rename can land
 * before the contents do, and a power loss leaves an intact name over an
 * empty file. */
static bool fsync_stream(FILE *f) {
#if defined(_WIN32)
    return _commit(_fileno(f)) == 0;
#else
    return fsync(fileno(f)) == 0;
#endif
}

static void copy_permissions(const char *from, const char *to) {
#if defined(_WIN32)
    (void)from;
    (void)to;
#else
    struct stat st;
    if (stat(from, &st) == 0) {
        chmod(to, st.st_mode & 07777);
    } else {
        mode_t mask = umask(0);
        umask(mask);
        chmod(to, 0666 & ~mask);
    }
#endif
}

/* POSIX rename() replaces the destination; the Win32 one refuses. */
static bool rename_over(const char *from, const char *to) {
#if defined(_WIN32)
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(from, to) == 0;
#endif
}

/* A rename is only as durable as the directory entry recording it. Best
 * effort: a filesystem that refuses this is not one to fail a save over. */
static void fsync_parent_directory(const char *path) {
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

static bool write_in_place(const char *path, AseWriteFn write_fn, void *user) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    if (!write_fn(f, user)) {
        fclose(f);
        return false;
    }
    return fclose(f) == 0;
}

bool ase_write_atomically(const char *path, AseWriteFn write_fn, void *user) {
    if (path == NULL || write_fn == NULL) {
        return false;
    }

    /* Through a symlink, not over it: replacing the link is how an
     * editor silently detaches a dotfile from the repository it is
     * checked into. A path that does not exist yet resolves to itself. */
    char *resolved = real_path(path);
    const char *target = (resolved != NULL) ? resolved : path;

    char *tmp = temp_path_beside(target);
    if (tmp == NULL) {
        free(resolved);
        return false;
    }

    FILE *f = open_private(tmp);
    if (f == NULL) {
        /* A directory that is not writable while the file inside it is.
         * Refusing to save would strand work the user cannot get out any
         * other way, which is also data loss, so this falls back to the
         * unsafe write. It is the only remaining path that can lose
         * data, and it is no worse than what every save did before. */
        free(tmp);
        bool ok = write_in_place(target, write_fn, user);
        free(resolved);
        return ok;
    }

    bool ok = write_fn(f, user);
    if (ok) {
        ok = (fflush(f) == 0) && fsync_stream(f);
    }
    if (fclose(f) != 0) {
        ok = false;
    }
    if (ok) {
        copy_permissions(target, tmp);
    }

    if (!ok || !rename_over(tmp, target)) {
        remove(tmp);
        free(tmp);
        free(resolved);
        return false;
    }

    fsync_parent_directory(target);
    free(tmp);
    free(resolved);
    return true;
}
