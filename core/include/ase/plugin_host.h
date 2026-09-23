#ifndef ASE_PLUGIN_HOST_H
#define ASE_PLUGIN_HOST_H

#include <stdbool.h>
#include <stddef.h>

#include "ase/buffer.h"
#include "ase/editor_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A single command registry populated by both Lua scripts and native
 * (dlopen'd) plugins — see docs/adr/0009-plugin-abi-and-lua-host.md for
 * why these share one registry instead of being two separate systems.
 */

typedef struct AsePluginHost AsePluginHost;

/* A command a plugin registers: acts on the editor through `ctx` — its
 * buffer, caret, selection, config and status line. `user_data` is
 * whatever the registering plugin attached at registration time (NULL
 * for Lua-backed commands — the Lua closure carries its own state).
 *
 * Took a bare AseBuffer * before ABI 2; see docs/adr/0141. */
typedef void (*AseCommandFn)(AseEditorContext *ctx, void *user_data);

AsePluginHost *ase_plugin_host_create(void);
void ase_plugin_host_destroy(AsePluginHost *host);

/* Re-registering an existing name replaces it. Returns false only on
 * allocation failure. */
bool ase_plugin_host_register_command(AsePluginHost *host, const char *name,
                                       AseCommandFn fn, void *user_data);

/* Returns false if no command named `name` is registered. `ctx` may be
 * NULL, which a command sees as an editor that answers nothing. */
bool ase_plugin_host_run_command(AsePluginHost *host, const char *name, AseEditorContext *ctx);

size_t ase_plugin_host_command_count(const AsePluginHost *host);

/* The name of command `index`, or NULL when out of range. Valid until
 * the host is destroyed or another command is registered. */
const char *ase_plugin_host_command_name(const AsePluginHost *host, size_t index);

/* Loads every .lua script (run through the embedded Lua VM) and every
 * native plugin (.so / .dylib / .dll per platform, via dlopen) in `dir`,
 * non-recursively. Both register commands into `host` the same way —
 * see docs/adr/0009. A missing directory is not an error (returns 0).
 * A script/plugin that errors during load is skipped (logged to
 * stderr) rather than aborting the rest of the load. Returns the
 * number of files successfully loaded. */
int ase_plugin_host_load_directory(AsePluginHost *host, const char *dir);

#ifdef __cplusplus
}
#endif

#endif /* ASE_PLUGIN_HOST_H */
