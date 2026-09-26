/* Test fixture for ase_plugin_host_tests (core/tests/test_plugin_host.c):
 * a minimal native plugin exercising the real ase_plugin_abi.h contract,
 * built as a loadable module and dlopen'd by the test, not linked
 * directly. Reverses the whole buffer. */

#include <stdlib.h>

#include "ase/plugin_abi.h"

static void reverse_command(AseEditorContext *ctx, void *user_data) {
    (void)user_data;

    AseBuffer *buffer = ase_ctx_buffer(ctx);
    size_t len = ase_buffer_length(buffer);
    if (len == 0) {
        return;
    }

    char *text = (char *)malloc(len);
    if (text == NULL) {
        return;
    }
    ase_buffer_get_text(buffer, 0, len, text);

    for (size_t i = 0; i < len / 2; i++) {
        char tmp = text[i];
        text[i] = text[len - 1 - i];
        text[len - 1 - i] = tmp;
    }

    ase_buffer_delete(buffer, 0, len);
    ase_buffer_insert(buffer, 0, text, len);
    free(text);

    /* Reversing moves everything, so the caret means nothing where it
     * was: park it at the start. Also proves the context is writable. */
    ase_ctx_set_cursor(ctx, 0);
    ase_ctx_status(ctx, "reversed");
}

ASE_PLUGIN_ABI;

ASE_PLUGIN_EXPORT void ase_plugin_register(AsePluginHost *host, const AsePluginApi *api) {
    api->register_command(host, "native_reverse", reverse_command, NULL);
}
