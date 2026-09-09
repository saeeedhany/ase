#include "ase/plugin_host.h"
#include "ase/plugin_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#endif

/* Plain ISO C90 strdup — avoids relying on POSIX strdup/MSVC's _strdup. */
static char *ase_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len);
    return copy;
}

typedef struct {
    char *name;
    AseCommandFn fn;
    void *user_data;
    bool owns_user_data; /* true for Lua-backed commands' context struct */
} CommandEntry;

struct AsePluginHost {
    lua_State *L;

    CommandEntry *commands;
    size_t command_count;
    size_t command_capacity;

    void **native_handles;
    size_t native_handle_count;
    size_t native_handle_capacity;
};

/* Every Lua-registered command gets one of these as its AseCommandFn
 * user_data, freed when the command is replaced or the host is
 * destroyed (owns_user_data = true). */
typedef struct {
    AsePluginHost *host;
    int lua_ref;
} LuaCommandContext;

static bool plugin_host_register_command_owned(AsePluginHost *host, const char *name,
                                                 AseCommandFn fn, void *user_data,
                                                 bool owns_user_data) {
    for (size_t i = 0; i < host->command_count; i++) {
        if (strcmp(host->commands[i].name, name) == 0) {
            if (host->commands[i].owns_user_data) {
                free(host->commands[i].user_data);
            }
            host->commands[i].fn = fn;
            host->commands[i].user_data = user_data;
            host->commands[i].owns_user_data = owns_user_data;
            return true;
        }
    }

    if (host->command_count == host->command_capacity) {
        size_t new_cap = host->command_capacity == 0 ? 8 : host->command_capacity * 2;
        CommandEntry *grown = (CommandEntry *)realloc(host->commands, new_cap * sizeof(CommandEntry));
        if (grown == NULL) {
            return false;
        }
        host->commands = grown;
        host->command_capacity = new_cap;
    }

    char *name_copy = ase_strdup(name);
    if (name_copy == NULL) {
        return false;
    }

    host->commands[host->command_count].name = name_copy;
    host->commands[host->command_count].fn = fn;
    host->commands[host->command_count].user_data = user_data;
    host->commands[host->command_count].owns_user_data = owns_user_data;
    host->command_count++;
    return true;
}

static void lua_command_trampoline(AseBuffer *buffer, void *user_data) {
    LuaCommandContext *ctx = (LuaCommandContext *)user_data;
    lua_State *L = ctx->host->L;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->lua_ref);
    lua_pushlightuserdata(L, buffer);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        fprintf(stderr, "ase: lua command error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

static int l_register_command(lua_State *L) {
    AsePluginHost *host = (AsePluginHost *)lua_touserdata(L, lua_upvalueindex(1));
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    LuaCommandContext *ctx = (LuaCommandContext *)malloc(sizeof(LuaCommandContext));
    if (ctx == NULL) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return 0;
    }
    ctx->host = host;
    ctx->lua_ref = ref;

    if (!plugin_host_register_command_owned(host, name, lua_command_trampoline, ctx, true)) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        free(ctx);
    }
    return 0;
}

static AseBuffer *check_buffer_arg(lua_State *L, int index) {
    return (AseBuffer *)lua_touserdata(L, index);
}

static int l_buffer_length(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)ase_buffer_length(check_buffer_arg(L, 1)));
    return 1;
}

static int l_buffer_get_text(lua_State *L) {
    AseBuffer *buf = check_buffer_arg(L, 1);
    lua_Integer at = luaL_checkinteger(L, 2);
    lua_Integer len = luaL_checkinteger(L, 3);
    if (at < 0 || len < 0) {
        lua_pushliteral(L, "");
        return 1;
    }

    char *scratch = (len > 0) ? (char *)malloc((size_t)len) : NULL;
    if (len > 0 && scratch == NULL) {
        lua_pushliteral(L, "");
        return 1;
    }

    size_t copied = ase_buffer_get_text(buf, (size_t)at, (size_t)len, scratch);
    lua_pushlstring(L, scratch, copied);
    free(scratch);
    return 1;
}

static int l_buffer_insert(lua_State *L) {
    AseBuffer *buf = check_buffer_arg(L, 1);
    lua_Integer at = luaL_checkinteger(L, 2);
    size_t len;
    const char *text = luaL_checklstring(L, 3, &len);
    lua_pushboolean(L, at >= 0 && ase_buffer_insert(buf, (size_t)at, text, len));
    return 1;
}

static int l_buffer_delete(lua_State *L) {
    AseBuffer *buf = check_buffer_arg(L, 1);
    lua_Integer at = luaL_checkinteger(L, 2);
    lua_Integer len = luaL_checkinteger(L, 3);
    lua_pushboolean(L, at >= 0 && len >= 0 && ase_buffer_delete(buf, (size_t)at, (size_t)len));
    return 1;
}

static void setup_lua_bindings(AsePluginHost *host) {
    lua_State *L = host->L;

    luaL_openlibs(L);

    lua_newtable(L);

    lua_pushcfunction(L, l_buffer_length);
    lua_setfield(L, -2, "buffer_length");
    lua_pushcfunction(L, l_buffer_get_text);
    lua_setfield(L, -2, "buffer_get_text");
    lua_pushcfunction(L, l_buffer_insert);
    lua_setfield(L, -2, "buffer_insert");
    lua_pushcfunction(L, l_buffer_delete);
    lua_setfield(L, -2, "buffer_delete");

    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, l_register_command, 1);
    lua_setfield(L, -2, "register_command");

    lua_setglobal(L, "ase");
}

AsePluginHost *ase_plugin_host_create(void) {
    AsePluginHost *host = (AsePluginHost *)calloc(1, sizeof(AsePluginHost));
    if (host == NULL) {
        return NULL;
    }

    host->L = luaL_newstate();
    if (host->L == NULL) {
        free(host);
        return NULL;
    }

    setup_lua_bindings(host);
    return host;
}

void ase_plugin_host_destroy(AsePluginHost *host) {
    if (host == NULL) {
        return;
    }

    for (size_t i = 0; i < host->command_count; i++) {
        free(host->commands[i].name);
        if (host->commands[i].owns_user_data) {
            free(host->commands[i].user_data);
        }
    }
    free(host->commands);

    if (host->L != NULL) {
        lua_close(host->L);
    }

    for (size_t i = 0; i < host->native_handle_count; i++) {
#if defined(_WIN32)
        FreeLibrary((HMODULE)host->native_handles[i]);
#else
        dlclose(host->native_handles[i]);
#endif
    }
    free(host->native_handles);

    free(host);
}

bool ase_plugin_host_register_command(AsePluginHost *host, const char *name,
                                       AseCommandFn fn, void *user_data) {
    if (host == NULL || name == NULL || fn == NULL) {
        return false;
    }
    return plugin_host_register_command_owned(host, name, fn, user_data, false);
}

bool ase_plugin_host_run_command(AsePluginHost *host, const char *name, AseBuffer *buffer) {
    if (host == NULL || name == NULL) {
        return false;
    }
    for (size_t i = 0; i < host->command_count; i++) {
        if (strcmp(host->commands[i].name, name) == 0) {
            host->commands[i].fn(buffer, host->commands[i].user_data);
            return true;
        }
    }
    return false;
}

size_t ase_plugin_host_command_count(const AsePluginHost *host) {
    return host == NULL ? 0 : host->command_count;
}

static bool has_suffix(const char *name, const char *suffix) {
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > name_len) {
        return false;
    }
    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

static bool is_native_plugin_file(const char *name) {
#if defined(_WIN32)
    return has_suffix(name, ".dll");
#elif defined(__APPLE__)
    return has_suffix(name, ".so") || has_suffix(name, ".dylib");
#else
    return has_suffix(name, ".so");
#endif
}

static bool is_lua_script_file(const char *name) {
    return has_suffix(name, ".lua");
}

static bool add_native_handle(AsePluginHost *host, void *handle) {
    if (host->native_handle_count == host->native_handle_capacity) {
        size_t new_cap = host->native_handle_capacity == 0 ? 4 : host->native_handle_capacity * 2;
        void **grown = (void **)realloc(host->native_handles, new_cap * sizeof(void *));
        if (grown == NULL) {
            return false;
        }
        host->native_handles = grown;
        host->native_handle_capacity = new_cap;
    }
    host->native_handles[host->native_handle_count++] = handle;
    return true;
}

static bool load_lua_script(AsePluginHost *host, const char *path) {
    if (luaL_dofile(host->L, path) != LUA_OK) {
        fprintf(stderr, "ase: failed to load plugin '%s': %s\n", path, lua_tostring(host->L, -1));
        lua_pop(host->L, 1);
        return false;
    }
    return true;
}

static bool load_native_plugin(AsePluginHost *host, const char *path) {
#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(path);
    if (handle == NULL) {
        fprintf(stderr, "ase: failed to load plugin '%s'\n", path);
        return false;
    }
    AsePluginRegisterFn register_fn = (AsePluginRegisterFn)GetProcAddress(handle, "ase_plugin_register");
#else
    void *handle = dlopen(path, RTLD_NOW);
    if (handle == NULL) {
        fprintf(stderr, "ase: failed to load plugin '%s': %s\n", path, dlerror());
        return false;
    }
    AsePluginRegisterFn register_fn = (AsePluginRegisterFn)dlsym(handle, "ase_plugin_register");
#endif

    if (register_fn == NULL) {
        fprintf(stderr, "ase: plugin '%s' has no ase_plugin_register symbol\n", path);
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return false;
    }

    static const AsePluginApi api = {ASE_PLUGIN_ABI_VERSION, ase_plugin_host_register_command};
    register_fn(host, &api);

    if (!add_native_handle(host, handle)) {
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return false;
    }

    return true;
}

int ase_plugin_host_load_directory(AsePluginHost *host, const char *dir) {
    if (host == NULL || dir == NULL) {
        return 0;
    }

    int loaded = 0;

#if defined(_WIN32)
    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s\\*", dir) >= (int)sizeof(pattern)) {
        return 0;
    }

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        char full_path[MAX_PATH];
        if (snprintf(full_path, sizeof(full_path), "%s\\%s", dir, data.cFileName) >= (int)sizeof(full_path)) {
            continue;
        }

        if (is_lua_script_file(data.cFileName)) {
            if (load_lua_script(host, full_path)) {
                loaded++;
            }
        } else if (is_native_plugin_file(data.cFileName)) {
            if (load_native_plugin(host, full_path)) {
                loaded++;
            }
        }
    } while (FindNextFileA(find, &data));

    FindClose(find);
#else
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char full_path[4096];
        if (snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name) >= (int)sizeof(full_path)) {
            continue;
        }

        if (is_lua_script_file(entry->d_name)) {
            if (load_lua_script(host, full_path)) {
                loaded++;
            }
        } else if (is_native_plugin_file(entry->d_name)) {
            if (load_native_plugin(host, full_path)) {
                loaded++;
            }
        }
    }
    closedir(dp);
#endif

    return loaded;
}
