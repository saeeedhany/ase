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

/*
 * The four things a plugin can react to. Deliberately four: each one is
 * a thing plugins actually want (format on save, lint on change, a
 * status widget, project tooling), and every extra one is a promise
 * about when it fires that has to hold forever. See docs/adr/0142.
 */
typedef enum {
    ASE_EVENT_BUFFER_CHANGED,
    ASE_EVENT_CURSOR_MOVED,
    ASE_EVENT_FILE_SAVED,
    ASE_EVENT_FILE_OPENED,
    ASE_EVENT_COUNT
} AseEventKind;

/* Same shape as a command: a hook is a command nobody typed. */
typedef void (*AseEventFn)(AseEditorContext *ctx, void *user_data);

/* The name used in config, in Lua and in messages — "buffer_changed" and
 * so on. NULL if `event` is out of range. */
const char *ase_event_name(AseEventKind event);
bool ase_event_from_name(const char *name, AseEventKind *out);

AsePluginHost *ase_plugin_host_create(void);
void ase_plugin_host_destroy(AsePluginHost *host);

/* Re-registering an existing name replaces it. Returns false only on
 * allocation failure. */
bool ase_plugin_host_register_command(AsePluginHost *host, const char *name,
                                       AseCommandFn fn, void *user_data);

/* Returns false if no command named `name` is registered. `ctx` may be
 * NULL, which a command sees as an editor that answers nothing. */
bool ase_plugin_host_run_command(AsePluginHost *host, const char *name, AseEditorContext *ctx);

/* Hooks run in registration order. The same function may be registered
 * more than once; nothing deduplicates. Returns false on allocation
 * failure or an out-of-range event. */
bool ase_plugin_host_on(AsePluginHost *host, AseEventKind event, AseEventFn fn, void *user_data);

/* Runs every hook for `event`. A hook that causes another event does not
 * re-enter: the inner emit is dropped, so no chain of hooks can loop.
 * See docs/adr/0142. */
void ase_plugin_host_emit(AsePluginHost *host, AseEventKind event, AseEditorContext *ctx);

size_t ase_plugin_host_hook_count(const AsePluginHost *host, AseEventKind event);

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
