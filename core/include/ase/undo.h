#ifndef ASE_UNDO_H
#define ASE_UNDO_H

#include <stdbool.h>
#include <stddef.h>

#include "ase/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Records edit intent (offset plus the bytes inserted or removed), not
 * buffer snapshots. See docs/adr/0018.
 *
 * A group is one user-visible action: a keystroke fanning out across N
 * cursors is still one group. Groups may not nest.
 */

typedef struct AseUndoStack AseUndoStack;

AseUndoStack *ase_undo_create(void);
void ase_undo_destroy(AseUndoStack *stack);

/* Snapshots the caller's cursor offsets so undo can restore them.
 * Ignored if a group is already open. */
void ase_undo_begin_group(AseUndoStack *stack, const size_t *cursors, size_t cursor_count);

/* Snapshots the post-edit cursor offsets for redo. An empty group is
 * discarded, not pushed: an empty keystroke must not clear redo
 * history. Ignored if no group is open. */
void ase_undo_end_group(AseUndoStack *stack, const size_t *cursors, size_t cursor_count);

/* Call right after a successful ase_buffer_insert, inside a group. A
 * zero-length insert or a call outside a group is ignored. */
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

/*
 * Identifies the buffer state the stack is in. A fresh stack is 0, "as
 * loaded"; every committed group produces a new, never-reused id, and
 * undo/redo move between existing states. Two observations are equal
 * iff the content is, so a caller records the id at save and compares
 * — an edit-then-undo correctly reports clean.
 *
 * Deliberately not a group count: undoing one group and typing another
 * lands on the same count with different content.
 */
size_t ase_undo_state_id(const AseUndoStack *stack);

#ifdef __cplusplus
}
#endif

#endif /* ASE_UNDO_H */
