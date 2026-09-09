#ifndef ASE_BUFFER_H
#define ASE_BUFFER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Piece-table text buffer. See docs/adr/0005-buffer-engine-piece-table.md
 * for why this representation was chosen over a rope, and its known
 * limitations. The representation is opaque and intentionally hidden
 * behind this header so it can be swapped later without touching callers.
 */

typedef struct AseBuffer AseBuffer;

/* Empty buffer. Returns NULL on allocation failure. */
AseBuffer *ase_buffer_create(void);

/* Loads the whole file into the buffer. Returns NULL if the file can't be
 * opened/read or on allocation failure. */
AseBuffer *ase_buffer_create_from_file(const char *path);

void ase_buffer_destroy(AseBuffer *buf);

/* Total content length in bytes. */
size_t ase_buffer_length(const AseBuffer *buf);

/* Number of lines, counting '\n' bytes; an empty buffer is 1 line.
 * O(total bytes) — see "Known limitations" in ADR 0005. */
size_t ase_buffer_line_count(const AseBuffer *buf);

/* Number of internal pieces. Not semantically meaningful to callers;
 * exposed for benchmarking/diagnostics only. */
size_t ase_buffer_piece_count(const AseBuffer *buf);

/* Inserts `len` bytes of `text` at byte offset `at`. `at` must be
 * <= ase_buffer_length(buf). Returns false on an out-of-range offset or
 * allocation failure; the buffer is left unmodified in that case. */
bool ase_buffer_insert(AseBuffer *buf, size_t at, const char *text, size_t len);

/* Deletes the `len` bytes starting at offset `at`. `[at, at+len)` must lie
 * within the buffer. Returns false if the range is out of bounds. */
bool ase_buffer_delete(AseBuffer *buf, size_t at, size_t len);

/* Copies up to `len` bytes starting at `at` into `out` (caller-owned,
 * must hold at least `len` bytes). Clamped to the buffer's actual length.
 * Returns the number of bytes actually copied. */
size_t ase_buffer_get_text(const AseBuffer *buf, size_t at, size_t len, char *out);

/* Writes the full buffer contents to `path`, overwriting it. */
bool ase_buffer_save_to_file(const AseBuffer *buf, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ASE_BUFFER_H */
