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

One global table, `ase`. Your command is handed a context — the editor,
not just its text — and every other function takes it:

- `ase.register_command(name, function(ctx) ... end)`
- `ase.buffer_length(ctx)`
- `ase.buffer_get_text(ctx, at, len)`
- `ase.buffer_insert(ctx, at, text)`
- `ase.buffer_delete(ctx, at, len)`
- `ase.cursor(ctx)` — byte offset of the caret
- `ase.set_cursor(ctx, offset)`
- `ase.selection(ctx)` — start and end, or nothing when nothing is selected
- `ase.set_selection(ctx, start, end)`
- `ase.config(ctx, key)` — any `config.ase` value, or nil
- `ase.status(ctx, message)` — say something in the status bar
- `ase.on(event, function(ctx) ... end)` — react to something

```lua
-- ~/.config/ase/plugins/uppercase.lua
ase.register_command("uppercase_all", function(ctx)
    local len = ase.buffer_length(ctx)
    local text = ase.buffer_get_text(ctx, 0, len)
    ase.buffer_delete(ctx, 0, len)
    ase.buffer_insert(ctx, 0, string.upper(text))
end)
```

That is a complete, working plugin.

Selection and caret make the commands people actually want:

```lua
-- ~/.config/ase/plugins/surround.lua
ase.register_command("surround", function(ctx)
    local a, b = ase.selection(ctx)
    if a == nil then
        ase.status(ctx, "select something first")
        return
    end
    ase.buffer_insert(ctx, b, ")")
    ase.buffer_insert(ctx, a, "(")
    ase.set_cursor(ctx, a)
end)
```

Insert at the end before the start, as above: inserting at the start
first would move everything after it, `b` included.

The selection you get is the one highlighted on screen. In Visual mode
that includes the character under the caret, as Vim's does.

## Reacting to things

A command waits to be asked. A hook runs when something happens:

```lua
-- ~/.config/ase/plugins/strip.lua
ase.on("file_saved", function(ctx)
    local len = ase.buffer_length(ctx)
    local text = ase.buffer_get_text(ctx, 0, len)
    local stripped = text:gsub("[ \t]+\n", "\n")
    if stripped ~= text then
        ase.buffer_delete(ctx, 0, len)
        ase.buffer_insert(ctx, 0, stripped)
    end
end)
```

Four events, and there will not be many more:

| event | when |
| --- | --- |
| `buffer_changed` | the text may have changed |
| `cursor_moved` | the caret is somewhere else |
| `file_saved` | the file is on disk |
| `file_opened` | a buffer is ready to be worked on |

Hooks run in the order they were registered, and the same function may
be registered more than once.

### What you can rely on

**`buffer_changed` and `cursor_moved` are coalesced.** Typing a word is
one `buffer_changed`, not one per keystroke, and holding `j` is one
`cursor_moved`. They describe what changed since the last one, not every
step in between. A caret that ends where it started never fires at all.

**A hook runs on the thread that draws, so it must be fast.** A hook
that takes 30ms makes the editor stutter. Shell out and come back later
rather than doing the work inline.

**What a hook does is not itself an event.** A `file_saved` hook that
edits the buffer does not trigger `buffer_changed`, and nothing a hook
does can trigger the event it is handling. Hooks cannot make each other
loop.

**A hook that edits is one undo step**, exactly like a command, so `u`
takes back what it did.

**`file_saved` fires after the write, and the file is written again if
your hook changed anything** — so a formatter gets what it produced onto
disk in one save, and the buffer is left clean rather than dirty.

## In C

The same four events, and `ase_plugin_host_on` to register:

```c
static void on_saved(AseEditorContext *ctx, void *user_data) {
    (void)user_data;
    ase_ctx_status(ctx, "saved");
}

ASE_PLUGIN_EXPORT void ase_plugin_register(AsePluginHost *host, const AsePluginApi *api) {
    (void)api;
    ase_plugin_host_on(host, ASE_EVENT_FILE_SAVED, on_saved, NULL);
}
```

Two symbols, one of them a macro:

```c
#include "ase/plugin_abi.h"

ASE_PLUGIN_ABI;

static void reverse_command(AseEditorContext *ctx, void *user_data) {
    (void)user_data;
    AseBuffer *buffer = ase_ctx_buffer(ctx);
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
    ase_ctx_set_cursor(ctx, 0);
}

ASE_PLUGIN_EXPORT void ase_plugin_register(AsePluginHost *host, const AsePluginApi *api) {
    api->register_command(host, "native_reverse", reverse_command, NULL);
}
```

Build it as a shared module and drop it in the plugins directory.

`ASE_PLUGIN_ABI;` is not optional. It declares which ABI the plugin was
built against, and a plugin without it is refused with a message rather
than called through a signature it does not have.

`ASE_PLUGIN_EXPORT` is what makes both symbols findable on Windows,
where a DLL exports nothing unless it says so. It is empty everywhere
else, so write it either way.

A crashing native plugin crashes the editor. That is the inherent cost of
native over Lua, not something this layer pretends to sandbox.

## What a plugin can do today

Read and change buffer text, read and move the caret, read and set the
selection, read any config value, and write to the status bar. In C that
is:

```c
AseBuffer  *ase_ctx_buffer(AseEditorContext *ctx);
size_t      ase_ctx_cursor(const AseEditorContext *ctx);
void        ase_ctx_set_cursor(AseEditorContext *ctx, size_t offset);
bool        ase_ctx_selection(const AseEditorContext *ctx, size_t *start, size_t *end);
void        ase_ctx_set_selection(AseEditorContext *ctx, size_t start, size_t end);
const char *ase_ctx_config(const AseEditorContext *ctx, const char *key);
void        ase_ctx_status(AseEditorContext *ctx, const char *message);
```

The handle is opaque and each capability is a function, so gaining one
is an added function rather than a changed struct — your plugin keeps
working across a version that adds something it does not call. See
[ADR 0141](../adr/0141-what-a-plugin-is-handed.md).

It can also react to four events — see above, and
[ADR 0142](../adr/0142-four-things-a-plugin-can-react-to.md).

It still cannot draw anything.

### One thing to know

A plugin command is one undo step. `u` takes back everything it did,
and the history from before it survives — running a formatter does not
cost you the rest of your session.

A caret you set is clamped to the buffer you leave behind, so moving it
past the end is harmless rather than a crash.

## Working examples

`core/tests/fixtures/` has a complete Lua plugin and a complete native
one, and `core/tests/test_plugin_host.c` shows how they are loaded and
invoked. Both are real tests, so they cannot rot.

The design rationale is [ADR 0009](../adr/0009-plugin-abi-and-lua-host.md)
for the registry and [ADR 0141](../adr/0141-what-a-plugin-is-handed.md)
for what a command is handed.
