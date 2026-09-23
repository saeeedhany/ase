# Extensibility plan: plugins and customization

**Status: proposal, not decided.** This is a recommendation for how to
get from what exists today to "easy to write plugins for, customizable at
every point." Nothing here is implemented. Each accepted piece should
become its own ADR when it's built.

## Where things actually stand

Worth being blunt, because the gap between "has a plugin system" and
"plugins are usable" is the whole problem:

- `core/` has a complete, tested plugin host — one command registry fed by
  both Lua scripts and `dlopen`'d native plugins (ADR 0009), with
  `ase_plugin_host_load_directory()` loading a whole folder.
- **It is not wired into the GUI.** `grep -rn "plugin_host\|ase_plugin" gui/src/`
  returns nothing. The editor cannot load or run a plugin today.
- The ABI a plugin sees is one function shape:
  `void (*)(AseBuffer *buffer, void *user_data)`.

So a plugin can mutate buffer text, and nothing else. It cannot read or
move the cursor, touch the selection, read config, bind a key, draw
anything, or react to an event. That's not a criticism of ADR 0009 —
it deliberately shipped the smallest thing that worked — but it is the
real starting point.

> Written before any of this was built. Every recommendation below has
> since been done, and each is marked. The command shape is now
> `void (*)(AseEditorContext *ctx, void *user_data)`, binding a key is
> config, and a plugin can react to four events.

Customization has the same shape: `config.ase` is genuinely live
(hot-reloaded, ADR 0008) and covers colors, font, animations, line
numbers, Vim mode, and the LSP/build commands. But **every keybinding in
the app is a hardcoded C++ constant** (ADR 0026, restated in ADR 0046),
and the whole UI layout is fixed.

## Recommendation 1 — wire the host in before widening the ABI

> **Done**, in [ADR 0054](adr/0054-multiple-buffers-and-plugin-host-wiring.md):
> `<config dir>/plugins/` loads at startup and `:name` runs any
> registered command. Since ADR 0113 a plugin command can also be bound
> to a key. The ABI is still the narrow one.


The single highest-value step, and it's small: load
`~/.config/ase/plugins/` at startup and route `:<name>` in the command
line to `ase_plugin_host_run_command()`.

That alone makes every plugin already writable against today's ABI
actually runnable, and it turns the command line into the plugin entry
point rather than a fixed `w`/`q`/`compile`/`output` list. `runCommand()`
already grew one argument-taking branch (`:<digits>`, ADR 0046), so the
dispatch is ready for it.

Do this *first*, deliberately, before widening the ABI — running real
plugins against the narrow ABI is what will tell you which parts of the
ABI actually need widening, rather than guessing.

## Recommendation 2 — give plugins a context, not more function pointers

> **Done**, in [ADR 0141](adr/0141-what-a-plugin-is-handed.md), at ABI 2.
> The shape below is what was built. Two things it did not say: a plugin
> built for ABI 1 is refused rather than called through the wrong
> signature, and Lua plugins needed no change at all, because the
> `ase.buffer_*` functions take the context now.


The temptation is to keep adding fields to `AsePluginApi`. Resist it: an
ABI that grows a pointer per feature is one that breaks on every release.

Pass commands an opaque `AseEditorContext *` instead of a bare `AseBuffer *`,
with accessor functions in the ABI:

```c
/* handle stays opaque; the struct can grow without breaking plugins */
size_t ase_ctx_cursor(const AseEditorContext *ctx);
void   ase_ctx_set_cursor(AseEditorContext *ctx, size_t offset);
bool   ase_ctx_selection(const AseEditorContext *ctx, size_t *start, size_t *end);
const char *ase_ctx_config(const AseEditorContext *ctx, const char *key);
void   ase_ctx_status(AseEditorContext *ctx, const char *message);
AseBuffer  *ase_ctx_buffer(AseEditorContext *ctx);
```

Why this shape specifically:

- **Opaque handle + accessors** means adding a capability is an added
  function, never a struct layout change — the ABI version can stay at 1
  far longer.
- **Cursor and selection are the missing 80%.** Almost every useful
  editor plugin (surround, comment toggle, align, sort selection, case
  convert) needs "where am I / what's selected" and nothing more exotic.
- **One mutation entry point matters.** Plugin edits must go through the
  same undo grouping as everything else, or a plugin edit becomes
  un-undoable. `ase_ctx_*` mutations should open exactly one
  `ase_undo_begin_group`/`end_group` per command invocation — the same
  invariant every Vim operator already follows (ADR 0046).

## Recommendation 3 — events, kept to a deliberately short list

> **Done**, in [ADR 0142](adr/0142-four-things-a-plugin-can-react-to.md).
> Both constraints below were taken: the two hot events are coalesced
> onto a timer that only runs while something is listening, and
> re-entry is guarded rather than mutation forbidden. Two things the
> sketch did not anticipate — a hook's edit needed the same one-step
> undo recording a command's does, and `file_saved` had to write the
> file again when a hook reformats, or format-on-save takes two saves.


Commands alone are pull-only; a plugin can't react. Add a small hook
registry:

```c
ase_plugin_on(host, ASE_EVENT_BUFFER_CHANGED, fn, user_data);
```

Start with four and no more: `BUFFER_CHANGED`, `CURSOR_MOVED`,
`FILE_SAVED`, `FILE_OPENED`. That covers formatters-on-save, linters,
status widgets, and project tooling.

Two constraints worth writing into the ADR up front:

- `BUFFER_CHANGED` fires from `refreshCache()`, which is already the
  per-keystroke choke point — so a slow plugin hook directly costs typing
  latency. Hooks should be documented as "must be fast," and ideally
  coalesced (fire once per idle tick, not once per keystroke).
- A hook that edits the buffer re-enters `refreshCache()`. Either forbid
  mutation from `BUFFER_CHANGED` or guard against re-entry — decide
  explicitly rather than discovering it as a crash.

## Recommendation 4 — make keybindings data, and do it once for everything

> **Done**, in [ADR 0113](adr/0113-keybindings-as-data.md). The shape
> below is what was built, with two differences: the family is
> `key.<chord> = <command>` rather than a `[keys]` section, because
> `config.ase` has no sections and already has two dotted families; and
> Vim's own `dd`/`gg` sequences are still not chords, so the
> `normal:dd` example remains undone. Everything else — the shared
> registry, plugin commands bindable with no new API, mode-qualified
> bindings — is in.


This is the biggest customization gap and it is **not Vim-specific** —
ADR 0046 already flagged that building rebinding just for Vim would be
the wrong shape.

Proposal: a `[keys]` section in `config.ase`, mapping a chord to a
*command name*, where built-in actions are registered into the same
registry plugins use:

```
[keys]
ctrl+s      = editor.save
ctrl+d      = editor.cursor.add-next-occurrence
ctrl+;      = editor.command-line
normal:dd   = vim.delete-line
```

The important design consequence: **built-in commands and plugin commands
become the same kind of thing.** A user rebinding Ctrl+S and a plugin
registering `myplugin.reformat` are then the same mechanism, and a plugin
can be bound to a key without any new API. That is what makes the system
feel customizable "at all points" rather than in a few blessed places.

This is a sizable feature. It needs: a chord parser, a mode-aware lookup
(Normal/Insert/Visual prefixes), conflict resolution against existing
hardcoded chords, and a reasonable error story for a config that binds
nonsense. Worth its own ADR.

## Recommendation 5 — theming beyond the current fixed key set

`config.ase` colors are currently a fixed list the C code knows by name.
Two additions, in order of value:

1. **Per-capture syntax colors as data.** Today `syntax_type` and
   `syntax_string` are hardcoded keys (ADR 0048) and the other captures
   are hardcoded alpha tiers. A `syntax.<capture> = <color|dim|bold>`
   convention lets a theme cover captures the C code has never heard of —
   which matters as soon as a second language is added.
2. **Theme files.** `theme = gruvbox` loading
   `~/.config/ase/themes/gruvbox.ase`, with the same hot-reload. Themes
   are the single most-asked-for customization in any editor, and this is
   a small change on top of the existing config loader.

Deliberately *not* recommended: exposing the motion tiers (ADR 0053) as
user config. The durations are a coherent designed language; letting each
one be set independently is how you get an app that feels arbitrary.
A single `animation_speed` multiplier over all of them would be the
right shape if this is ever wanted.

## Suggested order

> All five are done.


The ordering matters more than the list — each step makes the next one
informed rather than speculative:

1. Wire the host into the GUI (`:<name>` runs plugin commands) —
   [ADR 0054](adr/0054-multiple-buffers-and-plugin-host-wiring.md).
2. `AseEditorContext` with cursor/selection/config accessors —
   [ADR 0141](adr/0141-what-a-plugin-is-handed.md).
3. Keybindings as data, with built-ins registered as commands —
   [ADR 0113](adr/0113-keybindings-as-data.md).
4. Events — [ADR 0142](adr/0142-four-things-a-plugin-can-react-to.md).
5. Theme files + per-capture syntax colors —
   [ADR 0133](adr/0133-a-theme-you-can-share-as-a-file.md) and
   [ADR 0140](adr/0140-the-styles-a-capture-can-have.md).

Steps 1–2 were small and unlock most real plugins. Step 3 was the large
one, and is what turned "configurable" into "customizable at all points."
