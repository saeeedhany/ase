#include "ase/undo.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t offset;
    /* Either an insert or a delete, never both, so no kind tag is
     * needed: undo/redo check which side is set. */
    char *inserted;
    size_t inserted_len;
    char *removed;
    size_t removed_len;
} UndoEntry;

typedef struct {
    UndoEntry *entries;
    size_t count;
    size_t capacity;

    /* The state this group produces. Assigned on commit, never
     * reused -- see the contract in undo.h. */
    size_t seq;

    size_t *cursors_before;
    size_t cursors_before_count;
    size_t *cursors_after;
    size_t cursors_after_count;
} UndoGroup;

struct AseUndoStack {
    UndoGroup *undo_stack;
    size_t undo_count;
    size_t undo_capacity;

    UndoGroup *redo_stack;
    size_t redo_count;
    size_t redo_capacity;

    UndoGroup pending;
    bool has_pending;

    /* next_seq only increases; current_seq is the state the buffer is
     * in now, 0 being as loaded. */
    size_t next_seq;
    size_t current_seq;
};

static size_t *dup_cursors(const size_t *cursors, size_t count) {
    if (count == 0) {
        return NULL;
    }
    size_t *copy = (size_t *)malloc(count * sizeof(size_t));
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, cursors, count * sizeof(size_t));
    return copy;
}

static void group_free(UndoGroup *group) {
    for (size_t i = 0; i < group->count; i++) {
        free(group->entries[i].inserted);
        free(group->entries[i].removed);
    }
    free(group->entries);
    free(group->cursors_before);
    free(group->cursors_after);
}

static bool group_ensure_capacity(UndoGroup *group) {
    if (group->count < group->capacity) {
        return true;
    }
    size_t new_cap = group->capacity == 0 ? 4 : group->capacity * 2;
    UndoEntry *grown = (UndoEntry *)realloc(group->entries, new_cap * sizeof(UndoEntry));
    if (grown == NULL) {
        return false;
    }
    group->entries = grown;
    group->capacity = new_cap;
    return true;
}

static bool groups_ensure_capacity(UndoGroup **groups, size_t count, size_t *capacity) {
    if (count < *capacity) {
        return true;
    }
    size_t new_cap = *capacity == 0 ? 8 : *capacity * 2;
    UndoGroup *grown = (UndoGroup *)realloc(*groups, new_cap * sizeof(UndoGroup));
    if (grown == NULL) {
        return false;
    }
    *groups = grown;
    *capacity = new_cap;
    return true;
}

static void clear_redo(AseUndoStack *stack) {
    for (size_t i = 0; i < stack->redo_count; i++) {
        group_free(&stack->redo_stack[i]);
    }
    stack->redo_count = 0;
}

AseUndoStack *ase_undo_create(void) {
    return (AseUndoStack *)calloc(1, sizeof(AseUndoStack));
}

void ase_undo_destroy(AseUndoStack *stack) {
    if (stack == NULL) {
        return;
    }
    for (size_t i = 0; i < stack->undo_count; i++) {
        group_free(&stack->undo_stack[i]);
    }
    free(stack->undo_stack);
    for (size_t i = 0; i < stack->redo_count; i++) {
        group_free(&stack->redo_stack[i]);
    }
    free(stack->redo_stack);
    if (stack->has_pending) {
        group_free(&stack->pending);
    }
    free(stack);
}

void ase_undo_begin_group(AseUndoStack *stack, const size_t *cursors, size_t cursor_count) {
    if (stack == NULL || stack->has_pending) {
        return;
    }
    memset(&stack->pending, 0, sizeof(UndoGroup));
    stack->pending.cursors_before = dup_cursors(cursors, cursor_count);
    stack->pending.cursors_before_count = cursor_count;
    stack->has_pending = true;
}

void ase_undo_end_group(AseUndoStack *stack, const size_t *cursors, size_t cursor_count) {
    if (stack == NULL || !stack->has_pending) {
        return;
    }
    stack->has_pending = false;

    if (stack->pending.count == 0) {
        /* Every operation was a no-op, and a no-op keystroke must not
         * wipe redo history. */
        group_free(&stack->pending);
        memset(&stack->pending, 0, sizeof(UndoGroup));
        return;
    }

    stack->pending.cursors_after = dup_cursors(cursors, cursor_count);
    stack->pending.cursors_after_count = cursor_count;

    /* A new edit invalidates redo history. */
    clear_redo(stack);

    if (!groups_ensure_capacity(&stack->undo_stack, stack->undo_count, &stack->undo_capacity)) {
        group_free(&stack->pending);
        memset(&stack->pending, 0, sizeof(UndoGroup));
        return;
    }
    stack->next_seq++;
    stack->pending.seq = stack->next_seq;
    stack->current_seq = stack->pending.seq;

    stack->undo_stack[stack->undo_count] = stack->pending;
    stack->undo_count++;
    memset(&stack->pending, 0, sizeof(UndoGroup));
}

void ase_undo_record_insert(AseUndoStack *stack, size_t offset, const char *text, size_t len) {
    if (stack == NULL || !stack->has_pending || len == 0) {
        return;
    }
    if (!group_ensure_capacity(&stack->pending)) {
        return;
    }
    char *copy = (char *)malloc(len);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, text, len);

    UndoEntry *entry = &stack->pending.entries[stack->pending.count];
    entry->offset = offset;
    entry->inserted = copy;
    entry->inserted_len = len;
    entry->removed = NULL;
    entry->removed_len = 0;
    stack->pending.count++;
}

void ase_undo_record_delete(AseUndoStack *stack, size_t offset, const char *text, size_t len) {
    if (stack == NULL || !stack->has_pending || len == 0) {
        return;
    }
    if (!group_ensure_capacity(&stack->pending)) {
        return;
    }
    char *copy = (char *)malloc(len);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, text, len);

    UndoEntry *entry = &stack->pending.entries[stack->pending.count];
    entry->offset = offset;
    entry->inserted = NULL;
    entry->inserted_len = 0;
    entry->removed = copy;
    entry->removed_len = len;
    stack->pending.count++;
}

bool ase_undo_undo(AseUndoStack *stack, AseBuffer *buffer, size_t **out_cursors, size_t *out_count) {
    if (stack == NULL || stack->undo_count == 0) {
        return false;
    }

    UndoGroup group = stack->undo_stack[stack->undo_count - 1];

    /* Each entry's offset is only valid in the state right before that
     * entry was applied, so unwinding must walk in exact reverse of
     * application order, not grouped by offset. */
    for (size_t i = group.count; i-- > 0;) {
        UndoEntry *entry = &group.entries[i];
        if (entry->inserted_len > 0) {
            ase_buffer_delete(buffer, entry->offset, entry->inserted_len);
        }
        if (entry->removed_len > 0) {
            ase_buffer_insert(buffer, entry->offset, entry->removed, entry->removed_len);
        }
    }

    stack->undo_count--;
    /* Set before the redo-push, whose failure path must not skip it. */
    stack->current_seq = (stack->undo_count > 0) ? stack->undo_stack[stack->undo_count - 1].seq : 0;

    *out_cursors = dup_cursors(group.cursors_before, group.cursors_before_count);
    *out_count = group.cursors_before_count;

    if (!groups_ensure_capacity(&stack->redo_stack, stack->redo_count, &stack->redo_capacity)) {
        /* The undo already succeeded; losing this one redo is a
         * fallback, not a failure to report. */
        group_free(&group);
        return true;
    }
    stack->redo_stack[stack->redo_count] = group;
    stack->redo_count++;
    return true;
}

bool ase_undo_redo(AseUndoStack *stack, AseBuffer *buffer, size_t **out_cursors, size_t *out_count) {
    if (stack == NULL || stack->redo_count == 0) {
        return false;
    }

    UndoGroup group = stack->redo_stack[stack->redo_count - 1];

    /* Forward order, which keeps every recorded offset valid. */
    for (size_t i = 0; i < group.count; i++) {
        UndoEntry *entry = &group.entries[i];
        if (entry->removed_len > 0) {
            ase_buffer_delete(buffer, entry->offset, entry->removed_len);
        }
        if (entry->inserted_len > 0) {
            ase_buffer_insert(buffer, entry->offset, entry->inserted, entry->inserted_len);
        }
    }

    stack->redo_count--;
    stack->current_seq = group.seq;

    *out_cursors = dup_cursors(group.cursors_after, group.cursors_after_count);
    *out_count = group.cursors_after_count;

    if (!groups_ensure_capacity(&stack->undo_stack, stack->undo_count, &stack->undo_capacity)) {
        group_free(&group);
        return true;
    }
    stack->undo_stack[stack->undo_count] = group;
    stack->undo_count++;
    return true;
}

size_t ase_undo_state_id(const AseUndoStack *stack) {
    return (stack == NULL) ? 0 : stack->current_seq;
}
