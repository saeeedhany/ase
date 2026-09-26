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

#define ASE_PLUGIN_ABI_VERSION 2

/* A Windows DLL exports nothing unless it says so, and the loader finds
 * a plugin's symbols by name — so without this the plugin loads and
 * looks empty. Nothing on the platforms where every symbol is already
 * visible. See docs/adr/0143. */
#if defined(_WIN32)
#define ASE_PLUGIN_EXPORT __declspec(dllexport)
#else
#define ASE_PLUGIN_EXPORT
#endif

/* Handed to a native plugin's ase_plugin_register(). Still small:
 * what a command can *do* lives on AseEditorContext, which grows by
 * gaining functions rather than by this struct gaining pointers. */
typedef struct {
    int abi_version;
    bool (*register_command)(AsePluginHost *host, const char *name,
                              AseCommandFn fn, void *user_data);
} AsePluginApi;

/* Every native plugin must export exactly these two symbols (both
 * looked up by name via dlsym/GetProcAddress):
 *
 *     ASE_PLUGIN_ABI;
 *     ASE_PLUGIN_EXPORT void ase_plugin_register(AsePluginHost *host,
 *                                                 const AsePluginApi *api);
 *
 * ase_plugin_register is called once, immediately after the plugin is
 * loaded. Register your commands via api->register_command and return.
 *
 * The version symbol is what stops a plugin built against an older ABI
 * from being called through the current one — the command signature
 * changed in 2, and without the check that is a wrong-type call rather
 * than a message. A plugin that does not export it is refused. */
#define ASE_PLUGIN_ABI ASE_PLUGIN_EXPORT const int ase_plugin_abi_version = ASE_PLUGIN_ABI_VERSION

typedef void (*AsePluginRegisterFn)(AsePluginHost *host, const AsePluginApi *api);

#ifdef __cplusplus
}
#endif

#endif /* ASE_PLUGIN_ABI_H */
