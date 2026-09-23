#include "ase/editor_context.h"

#include <stdlib.h>

/*
 * Nothing but indirection, deliberately: the editor's own state stays in
 * the editor. What this buys is the shape — see docs/adr/0141.
 */
struct AseEditorContext {
    const AseEditorContextOps *ops;
    void *host;
};

AseEditorContext *ase_editor_context_create(const AseEditorContextOps *ops, void *host) {
    if (ops == NULL) {
        return NULL;
    }
    AseEditorContext *ctx = (AseEditorContext *)calloc(1, sizeof(AseEditorContext));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->ops = ops;
    ctx->host = host;
    return ctx;
}

void ase_editor_context_destroy(AseEditorContext *ctx) {
    free(ctx);
}

AseBuffer *ase_ctx_buffer(AseEditorContext *ctx) {
    if (ctx == NULL || ctx->ops->buffer == NULL) {
        return NULL;
    }
    return ctx->ops->buffer(ctx->host);
}

size_t ase_ctx_cursor(const AseEditorContext *ctx) {
    if (ctx == NULL || ctx->ops->cursor == NULL) {
        return 0;
    }
    return ctx->ops->cursor(ctx->host);
}

void ase_ctx_set_cursor(AseEditorContext *ctx, size_t offset) {
    if (ctx == NULL || ctx->ops->set_cursor == NULL) {
        return;
    }
    ctx->ops->set_cursor(ctx->host, offset);
}

bool ase_ctx_selection(const AseEditorContext *ctx, size_t *start, size_t *end) {
    if (ctx == NULL || ctx->ops->selection == NULL || start == NULL || end == NULL) {
        return false;
    }
    return ctx->ops->selection(ctx->host, start, end);
}

void ase_ctx_set_selection(AseEditorContext *ctx, size_t start, size_t end) {
    if (ctx == NULL || ctx->ops->set_selection == NULL) {
        return;
    }
    if (start > end) {
        size_t swap = start;
        start = end;
        end = swap;
    }
    ctx->ops->set_selection(ctx->host, start, end);
}

const char *ase_ctx_config(const AseEditorContext *ctx, const char *key) {
    if (ctx == NULL || ctx->ops->config == NULL || key == NULL) {
        return NULL;
    }
    return ctx->ops->config(ctx->host, key);
}

void ase_ctx_status(AseEditorContext *ctx, const char *message) {
    if (ctx == NULL || ctx->ops->status == NULL || message == NULL) {
        return;
    }
    ctx->ops->status(ctx->host, message);
}
