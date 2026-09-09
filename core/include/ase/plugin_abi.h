#ifndef ASE_PLUGIN_ABI_H
#define ASE_PLUGIN_ABI_H

#include "ase/plugin_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stable C ABI for compiled (native, dlopen'd) plugins. See
 * docs/adr/0009-plugin-abi-and-lua-host.md.
 */

#define ASE_PLUGIN_ABI_VERSION 1

/* Handed to a native plugin's ase_plugin_register(). Deliberately
 * small — registering commands is the whole surface for now. */
typedef struct {
    int abi_version;
    bool (*register_command)(AsePluginHost *host, const char *name,
                              AseCommandFn fn, void *user_data);
} AsePluginApi;

/* Every native plugin must export exactly this symbol (checked via
 * dlsym/GetProcAddress by name):
 *
 *     void ase_plugin_register(AsePluginHost *host, const AsePluginApi *api);
 *
 * Called once, immediately after the plugin is loaded. Register your
 * commands via api->register_command and return. */
typedef void (*AsePluginRegisterFn)(AsePluginHost *host, const AsePluginApi *api);

#ifdef __cplusplus
}
#endif

#endif /* ASE_PLUGIN_ABI_H */
