#ifndef ASE_EDITOR_CONTEXT_H
#define ASE_EDITOR_CONTEXT_H

#include <stdbool.h>
#include <stddef.h>

#include "ase/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What a plugin command is handed: the editor, not just its text. See
 * docs/adr/0141.
 *
 * The handle is opaque and every capability is a function, so gaining
 * one is an added function rather than a changed struct — plugins keep
 * working across a version that adds something they do not call.
 */

typedef struct AseEditorContext AseEditorContext;

/*
 * How the editor answers. Filled in by whoever owns the editor — the GUI
 * in the real app, a test's stand-in otherwise — and never seen by a
 * plugin, so this one *can* grow fields freely.
 *
 * Any entry may be NULL: a host that cannot do something says so by
 * leaving it out, and the accessor returns the documented empty answer
 * rather than crashing.
 */
typedef struct {
    AseBuffer *(*buffer)(void *host);
    size_t (*cursor)(void *host);
    void (*set_cursor)(void *host, size_t offset);
    bool (*selection)(void *host, size_t *start, size_t *end);
    void (*set_selection)(void *host, size_t start, size_t end);
    const char *(*config)(void *host, const char *key);
    void (*status)(void *host, const char *message);
} AseEditorContextOps;

/* `ops` is borrowed, not copied, so it must outlive the context — a
 * static table in practice. */
AseEditorContext *ase_editor_context_create(const AseEditorContextOps *ops, void *host);
void ase_editor_context_destroy(AseEditorContext *ctx);

/* The buffer being edited, or NULL. */
AseBuffer *ase_ctx_buffer(AseEditorContext *ctx);

/* Byte offset of the caret. 0 when the host does not track one. */
size_t ase_ctx_cursor(const AseEditorContext *ctx);
void ase_ctx_set_cursor(AseEditorContext *ctx, size_t offset);

/* False when nothing is selected, leaving *start and *end untouched.
 * `start` is always the lower offset. */
bool ase_ctx_selection(const AseEditorContext *ctx, size_t *start, size_t *end);
void ase_ctx_set_selection(AseEditorContext *ctx, size_t start, size_t end);

/* A config value by key, or NULL if unset. Valid until the config
 * reloads, so copy it if you keep it. */
const char *ase_ctx_config(const AseEditorContext *ctx, const char *key);

/* Say something in the status bar. */
void ase_ctx_status(AseEditorContext *ctx, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* ASE_EDITOR_CONTEXT_H */
