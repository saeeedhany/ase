#ifndef ASE_UNDO_H
#define ASE_UNDO_H

#include <stdbool.h>
#include <stddef.h>

#include "ase/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Records edit intent (offset + the bytes inserted/removed), not buffer
 * snapshots, on top of an AseBuffer -- the diff-based design ADR 0005
 * anticipated when it picked a piece table. See docs/adr/0018.
 *
 * A "group" is one user-visible action: a single keystroke that fans out
 * across N multi-cursors is still one group, undoing/redoing all of it
 * together. Groups may not nest.
 */

typedef struct AseUndoStack AseUndoStack;

AseUndoStack *ase_undo_create(void);
void ase_undo_destroy(AseUndoStack *stack);

/* Opens a new group, snapshotting the caller's current cursor offsets
 * (copied in) so an undo of this group can restore them. Ignored if a
 * group is already open. */
void ase_undo_begin_group(AseUndoStack *stack, const size_t *cursors, size_t cursor_count);

/* Closes the open group, snapshotting the caller's cursor offsets as they
 * stand after the edit (for redo to restore). A group with zero recorded
 * entries (every cursor's operation was a no-op) is discarded rather than
 * pushed -- there's nothing to undo, and an empty keystroke shouldn't
 * clear redo history. Ignored if no group is open. */
void ase_undo_end_group(AseUndoStack *stack, const size_t *cursors, size_t cursor_count);

/* Records that `len` bytes of `text` were just inserted at `offset` --
 * call right after the matching successful ase_buffer_insert. Must be
 * called between begin_group/end_group; a zero-length insert or a call
 * outside an open group is silently ignored. */
void ase_undo_record_insert(AseUndoStack *stack, size_t offset, const char *text, size_t len);

/* Records that `len` bytes of `text` were just deleted from `offset` --
 * `text` must have been read (e.g. via ase_buffer_get_text) *before* the
 * matching ase_buffer_delete, since it won't exist in the buffer
 * afterward. Same call-site contract as ase_undo_record_insert. */
void ase_undo_record_delete(AseUndoStack *stack, size_t offset, const char *text, size_t len);

/* Reverses the most recently completed group against `buffer`, moving it
 * onto the redo side. On success, `*out_cursors` is set to a freshly
 * malloc'd array of `*out_count` offsets -- the cursor positions recorded
 * at that group's begin_group call -- which the caller must free().
 * Returns false (buffer and outputs untouched) if there's nothing to
 * undo. */
bool ase_undo_undo(AseUndoStack *stack, AseBuffer *buffer, size_t **out_cursors, size_t *out_count);

/* Re-applies the most recently undone group against `buffer`, moving it
 * back onto the undo side. On success, `*out_cursors` is the group's
 * end_group snapshot, malloc'd, caller must free(). Returns false if
 * there's nothing to redo. */
bool ase_undo_redo(AseUndoStack *stack, AseBuffer *buffer, size_t **out_cursors, size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* ASE_UNDO_H */
