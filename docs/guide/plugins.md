# Writing a plugin

A plugin is one file you drop in a directory. There is no marketplace, no
registry and no manifest.

```
~/.config/ase/plugins/
```

Everything in there is loaded at startup — `.lua` scripts and compiled
`.so` / `.dylib` / `.dll` modules alike. A missing directory is not an
error.

Both kinds register into the same named-command registry, so a Lua plugin
and a native one are indistinguishable from the outside.

## Running one

```
:uppercase_all
```

Or bind it, exactly like a built-in command:

```ini
key.alt+u = uppercase_all
```

The editor checks bindings against the commands that actually exist —
including the ones your plugins registered — so a binding at a name
nothing provides is reported when you save your config.

## In Lua

One global table, `ase`:

- `ase.register_command(name, function(buffer) ... end)`
- `ase.buffer_length(buffer)`
- `ase.buffer_get_text(buffer, at, len)`
- `ase.buffer_insert(buffer, at, text)`
- `ase.buffer_delete(buffer, at, len)`

```lua
-- ~/.config/ase/plugins/uppercase.lua
ase.register_command("uppercase_all", function(buf)
    local len = ase.buffer_length(buf)
    local text = ase.buffer_get_text(buf, 0, len)
    ase.buffer_delete(buf, 0, len)
    ase.buffer_insert(buf, 0, string.upper(text))
end)
```

That is a complete, working plugin.

## In C

Export exactly one symbol:

```c
#include "ase/plugin_abi.h"

static void reverse_command(AseBuffer *buffer, void *user_data) {
    (void)user_data;
    size_t len = ase_buffer_length(buffer);
    if (len == 0) {
        return;
    }
    char *text = malloc(len);
    ase_buffer_get_text(buffer, 0, len, text);
    for (size_t i = 0; i < len / 2; i++) {
        char t = text[i];
        text[i] = text[len - 1 - i];
        text[len - 1 - i] = t;
    }
    ase_buffer_delete(buffer, 0, len);
    ase_buffer_insert(buffer, 0, text, len);
    free(text);
}

void ase_plugin_register(AsePluginHost *host, const AsePluginApi *api) {
    api->register_command(host, "native_reverse", reverse_command, NULL);
}
```

Build it as a shared module and drop it in the plugins directory.

A crashing native plugin crashes the editor. That is the inherent cost of
native over Lua, not something this layer pretends to sandbox.

## What a plugin can do today

Read and change buffer text. That is the whole ABI:

```c
void (*)(AseBuffer *buffer, void *user_data)
```

It cannot yet read or move the cursor, touch the selection, read config,
draw anything, or react to an event. ADR 0009 deliberately shipped the
smallest thing that worked, and the plan for widening it — an opaque
context handle with accessors, rather than more function pointers — is
written up in [Extensibility](../EXTENSIBILITY.md).

### One thing to know

Running a plugin command discards the undo history for that buffer. The
plugin edits the buffer directly, underneath the undo stack, so the stack
can no longer describe what happened. The buffer stays marked as modified
so you cannot lose the file by accident, but you cannot `u` your way back
past the plugin.

## Working examples

`core/tests/fixtures/` has a complete Lua plugin and a complete native
one, and `core/tests/test_plugin_host.c` shows how they are loaded and
invoked. Both are real tests, so they cannot rot.

The design rationale is
[ADR 0009](../adr/0009-plugin-abi-and-lua-host.md).
