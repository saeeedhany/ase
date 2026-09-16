#ifndef ASE_INTERNAL_H
#define ASE_INTERNAL_H

#include <stdlib.h>
#include <string.h>

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

#endif
