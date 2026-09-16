#ifndef ASE_INTERNAL_H
#define ASE_INTERNAL_H

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>

/* Internal to core: none of this appears in a public header. */

char *ase_strdup(const char *s);
char *ase_memdup(const char *text, size_t len);

/* Creates the directory holding `path`, and its parents. Best effort. */
void ase_make_parent_dirs(const char *path);

/* Writes whatever `write_fn` emits to a temporary beside `path`, then
 * renames it over `path`. The rename is atomic, so `path` is either
 * entirely its old contents or entirely the new ones — never the half a
 * failed write leaves behind. Symlinks are resolved first, permissions
 * carried over, and the bytes fsynced before the rename. When no
 * temporary can be created the write goes straight to `path`, which is
 * the one case that can still lose data. See docs/adr/0109.
 *
 * `write_fn` returns false to abandon the write, leaving `path` alone. */
typedef bool (*AseWriteFn)(FILE *f, void *user);
bool ase_write_atomically(const char *path, AseWriteFn write_fn, void *user);

#endif
