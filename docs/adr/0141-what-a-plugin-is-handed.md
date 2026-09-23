# ADR 0141: What a plugin is handed

## Status

Accepted

## Context

[ADR 0009](0009-plugin-abi-and-lua-host.md) shipped the smallest plugin
ABI that worked, deliberately:

```c
void (*)(AseBuffer *buffer, void *user_data)
```

A plugin could change text. That is all. It could not read the caret,
see what was selected, read a config value, or say anything to the user.
Almost every plugin people actually write — surround, comment toggle,
align, sort the selection, convert case — needs "where am I / what is
selected" and nothing more exotic than that, so the ABI excluded most of
its own use cases.

[EXTENSIBILITY.md](../EXTENSIBILITY.md)'s recommendation 2 named the
answer and the reason: an opaque context handle with accessor functions,
not more fields on a struct. A struct that grows a pointer per feature
breaks every plugin on every release.

## Decision

A command is handed an `AseEditorContext *`. The handle is opaque and
every capability is a function, so gaining one is an added function
rather than a changed layout: a plugin keeps working across a version
that adds something it does not call.

```c
AseBuffer  *ase_ctx_buffer(AseEditorContext *ctx);
size_t      ase_ctx_cursor(const AseEditorContext *ctx);
void        ase_ctx_set_cursor(AseEditorContext *ctx, size_t offset);
bool        ase_ctx_selection(const AseEditorContext *ctx, size_t *start, size_t *end);
void        ase_ctx_set_selection(AseEditorContext *ctx, size_t start, size_t end);
const char *ase_ctx_config(const AseEditorContext *ctx, const char *key);
void        ase_ctx_status(AseEditorContext *ctx, const char *message);
```

### The editor answers, core only carries

The context holds a vtable the *editor* fills in and a `void *` back to
it. Core defines the shape and forwards; it has no idea what a caret is.

That vtable is the one struct here that may grow fields freely, because
no plugin ever sees it — it is filled by the GUI, which is compiled
against the same header. The opacity that protects plugins costs nothing
on the inside.

Every entry may be NULL, and every accessor returns the documented empty
answer when it is: a host that cannot do something says so by leaving it
out. A test's stand-in editor can implement three functions and be a
valid host, and a NULL context behaves the same again, so nothing needs
a null check at the call site.

### Cursor and selection are requests, not writes

`runPluginCommand()` takes the whole buffer apart and puts it back to
record the plugin's edit as one undo step
([ADR 0128](0128-one-name-one-command.md)). A caret written
straight into `m_cursors` during the command would be overwritten by
that rebuild.

So `set_cursor` and `set_selection` record what was asked for, and the
request is applied after the rebuild, clamped to the buffer the plugin
actually left behind. A plugin that deletes half the file and then asks
for a caret at the old end gets the new end instead of a crash.

### The selection a plugin sees is the one on screen

Vim's visual selection includes the character under the caret; this
editor's anchor/cursor pair does not
([ADR 0078](0078-visual-mode-replace.md)). Reading `selectionMaxAt()`
directly would have handed plugins a range one character shorter than
the highlight the user is looking at — `v` then four `l` over `hello`
would surround `hell`.

It goes through `vimVisualEnd()`, which is what the painter and every
visual operator already use. The test caught this on its first run.

### ABI 2, and a stale plugin is refused

The command signature changed, so the ABI version did. A native plugin
built for ABI 1 that got called through ABI 2 would take a context where
it expects a buffer — silent memory corruption, and nothing in ABI 1
would have caught it, because the version lived in the struct the *host*
hands the *plugin*, which a stale plugin is free to ignore.

Native plugins now export `ASE_PLUGIN_ABI;`, a one-line macro declaring
what they were built against. The loader reads it before calling
anything, and refuses a plugin that disagrees or omits it:

```
ase: plugin 'x.so' is built for ABI 0, this editor speaks 2
```

### Lua plugins did not change at all

A Lua command's argument is the context now rather than the buffer. But
`ase.buffer_length`, `ase.buffer_get_text`, `ase.buffer_insert` and
`ase.buffer_delete` take the context too and reach the buffer through
it, so a script written against ABI 1 passes back the value it was
handed and keeps working with no edit.

That was worth arranging rather than accepting a break: the alternative
made every existing script silently treat a context as a buffer.

## Consequences

Twenty tests. The ABI 1 Lua fixture is **deliberately left untouched**,
so the line that runs it is the proof that old scripts still work. A
second native fixture is built without the version symbol, lands in the
same directory as the good one, and the test asserts the command it
would have registered is absent — proving the refusal happens before
`ase_plugin_register` is called, not after.

Through the real editor: a Lua `surround` plugin over a visual selection
in the running app, which read the selection, edited both ends, moved
the caret and reported `surrounded 6 bytes, font=monospace` in the
status bar — selection, mutation, caret, config and status in one
command, none of which ABI 1 could express.

Clean under ASan and UBSan.

Not addressed: a `:` command run from Visual mode leaves the editor in
Visual with the selection collapsed. That predates this — a built-in
does the same — but plugins make it easy to hit, since acting on a
selection is now the common case.

Events are still the missing half. A plugin can be asked to act; it
cannot yet react.
