#include "ase/session.h"

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Format, deliberately the same shape as config.ase so it reads the same
 * way if anyone looks:
 *
 *   ASE-SESSION 1
 *   active = 2
 *   file = /home/u/one.c
 *   cursor = 42
 *   scroll = 3
 *   file = /home/u/two.c
 *   ...
 *
 * `file` opens an entry and `cursor`/`scroll` fill in the one most
 * recently opened, so a value arriving before any file is ignored
 * rather than misfiled. A path containing a newline cannot be
 * represented; nothing else is special, since everything after
 * "file = " up to the newline is the path, '=' included.
 */
static const char kHeader[] = "ASE-SESSION 1";

struct AseSession {
    AseSessionEntry *entries;
    size_t count;
    size_t capacity;
    size_t active;
};

AseSession *ase_session_create(void) {
    return (AseSession *)calloc(1, sizeof(AseSession));
}

void ase_session_destroy(AseSession *session) {
    if (session == NULL) {
        return;
    }
    for (size_t i = 0; i < session->count; i++) {
        free(session->entries[i].path);
    }
    free(session->entries);
    free(session);
}

bool ase_session_add(AseSession *session, const char *path, size_t cursor, long scroll_line) {
    if (session == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    if (session->count == session->capacity) {
        size_t next = session->capacity == 0 ? 8 : session->capacity * 2;
        AseSessionEntry *grown =
            (AseSessionEntry *)realloc(session->entries, next * sizeof(AseSessionEntry));
        if (grown == NULL) {
            return false;
        }
        session->entries = grown;
        session->capacity = next;
    }
    char *copy = ase_strdup(path);
    if (copy == NULL) {
        return false;
    }
    session->entries[session->count].path = copy;
    session->entries[session->count].cursor = cursor;
    session->entries[session->count].scroll_line = scroll_line;
    session->count++;
    return true;
}

size_t ase_session_count(const AseSession *session) {
    return session != NULL ? session->count : 0;
}

const AseSessionEntry *ase_session_entry(const AseSession *session, size_t index) {
    if (session == NULL || index >= session->count) {
        return NULL;
    }
    return &session->entries[index];
}

void ase_session_set_active(AseSession *session, size_t index) {
    if (session == NULL) {
        return;
    }
    session->active = (index < session->count) ? index : 0;
}

size_t ase_session_active(const AseSession *session) {
    if (session == NULL || session->active >= session->count) {
        return 0;
    }
    return session->active;
}

static bool write_session(FILE *f, void *user) {
    const AseSession *session = (const AseSession *)user;
    if (fprintf(f, "%s\nactive = %zu\n", kHeader, session->active) < 0) {
        return false;
    }
    for (size_t i = 0; i < session->count; i++) {
        const AseSessionEntry *e = &session->entries[i];
        /* A newline in a path would produce a file that reads back as
         * something else entirely, so such a path is left out. */
        if (strchr(e->path, '\n') != NULL) {
            continue;
        }
        if (fprintf(f, "file = %s\ncursor = %zu\nscroll = %ld\n", e->path, e->cursor,
                    e->scroll_line) < 0) {
            return false;
        }
    }
    return true;
}

bool ase_session_save(const AseSession *session, const char *path) {
    if (session == NULL || path == NULL) {
        return false;
    }
    /* Nothing open: remove the file rather than leave a stale one
     * claiming otherwise. */
    if (session->count == 0) {
        remove(path);
        return true;
    }
    ase_make_parent_dirs(path);
    return ase_write_atomically(path, write_session, (void *)session);
}

/* "key = value", with the value running to the end of the line. Returns
 * NULL when the line is not this key. */
static const char *value_for(const char *line, const char *key) {
    size_t key_len = strlen(key);
    if (strncmp(line, key, key_len) != 0) {
        return NULL;
    }
    const char *p = line + key_len;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '=') {
        return NULL;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

/* Strict: a value that is not entirely digits is not a number, and is
 * dropped rather than half-read. */
static bool parse_unsigned(const char *text, unsigned long long *out) {
    if (*text == '\0') {
        return false;
    }
    unsigned long long value = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + (unsigned long long)(*p - '0');
    }
    *out = value;
    return true;
}

AseSession *ase_session_load(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }

    char line[4096];
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        return NULL;
    }
    line[strcspn(line, "\r\n")] = '\0';
    if (strcmp(line, kHeader) != 0) {
        fclose(f);
        return NULL;
    }

    AseSession *session = ase_session_create();
    if (session == NULL) {
        fclose(f);
        return NULL;
    }

    unsigned long long active = 0;
    bool have_active = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        const char *value;
        if ((value = value_for(line, "file")) != NULL) {
            ase_session_add(session, value, 0, 0);
        } else if ((value = value_for(line, "cursor")) != NULL) {
            unsigned long long n;
            /* Only ever the entry most recently opened by `file`. */
            if (session->count > 0 && parse_unsigned(value, &n)) {
                session->entries[session->count - 1].cursor = (size_t)n;
            }
        } else if ((value = value_for(line, "scroll")) != NULL) {
            unsigned long long n;
            if (session->count > 0 && parse_unsigned(value, &n)) {
                session->entries[session->count - 1].scroll_line = (long)n;
            }
        } else if ((value = value_for(line, "active")) != NULL) {
            have_active = parse_unsigned(value, &active);
        }
    }
    fclose(f);

    if (session->count == 0) {
        ase_session_destroy(session);
        return NULL;
    }
    /* set_active clamps, so an index past the end reads back as 0. */
    ase_session_set_active(session, have_active ? (size_t)active : 0);
    return session;
}
