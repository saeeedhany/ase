/*
 * Test fixture for ase_plugin_host_tests: a native plugin built the way
 * ABI 1 plugins were, without the version symbol ABI 2 requires.
 *
 * It registers a command, so if the loader ever calls it the test can
 * see that it did. It must not — see docs/adr/0141.
 */

#include "ase/plugin_abi.h"

static void stale_command(AseEditorContext *ctx, void *user_data) {
    (void)ctx;
    (void)user_data;
}

void ase_plugin_register(AsePluginHost *host, const AsePluginApi *api) {
    api->register_command(host, "stale_command", stale_command, NULL);
}
