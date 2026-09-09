# ADR 0009: Plugin ABI + Lua host, unified under one command registry

## Status

Accepted

## Context

Spec section 4 names two distinct extension mechanisms: "Lua scripting
host for user-facing plugins/config logic" and "stable C ABI for
compiled feature modules." Section 3's architecture diagram places both
under Editor Core, not Feature Modules — unlike Tree-sitter (ADR 0007),
neither has a GUI or external-process dependency that would motivate
pulling it into `modules/`, so (correcting an earlier placeholder —
`modules/plugins/README.md` from the initial scaffold, before this
phase was designed) this lives in `core/`.

## Decisions

### 1. One command registry, two ways to populate it

`AsePluginHost` (`core/include/ase/plugin_host.h`) is a name → command
registry. A command is just `void fn(AseBuffer *buffer, void *user_data)`
— act on a buffer, nothing else, for v1. Both extension mechanisms feed
the *same* registry:

- **Lua**: `ase_plugin_host_load_directory` runs every `*.lua` file in a
  directory through an embedded Lua 5.4 VM. Scripts call
  `ase.register_command(name, fn)` — the Lua function is pinned via
  `luaL_ref` and invoked later through `lua_pcall`, so a buggy script
  can throw a Lua error without taking down the process (`lua_pcall`
  catches it; the error is logged to stderr and that command
  registration is simply skipped).
- **Native**: the same call also `dlopen`s every `*.so`/`*.dylib`/`*.dll`
  in that directory and calls the one required exported symbol,
  `ase_plugin_register(AsePluginHost*, const AsePluginApi*)`
  (`core/include/ase/plugin_abi.h`), handing it a tiny function-pointer
  table whose only member today is `register_command` — literally
  `ase_plugin_host_register_command` handed through, so a native plugin
  and a Lua script register into the identical registry through the
  identical function.

Unlike the Lua path, a native plugin can crash the whole process (it's
arbitrary compiled code) — that's an inherent, accepted cost of
"install-by-file" native plugins, no different from any other native
plugin system (Vim, VS Code's native extensions, etc.). Not something
sandboxing at this layer can fix; if it matters later, the mitigation is
process isolation (like the LSP client, ADR-to-come in Phase 6), not
something bolted onto `dlopen`.

### 2. Lua fetched via FetchContent, pinned to 5.4.7, not the newer 5.5 line

Same `FetchContent` pattern as Tree-sitter (ADR 0007) — Lua's official
mirror (`github.com/lua/lua`) ships no `CMakeLists.txt` at all, so there
was no upstream-build-system surprise to guard against this time (unlike
tree-sitter's `BUILD_SHARED_LIBS`). `core/CMakeLists.txt` globs every
`*.c` in the fetched source except `lua.c`/`luac.c` (the standalone
interpreter/compiler's own `main()`s, not part of the embeddable
library) and `ltests.c` (Lua's internal test harness, needs build hooks
this project doesn't set up). Pinned to `v5.4.7` rather than the newer
`v5.5.x` series released since — 5.4 is the version this project's C API
usage was actually written against and verified against; no reason to
take on an unfamiliar API surface's risk for a scripting host this early.

### 3. Buffer access only, for now

The Lua API surface is exactly `ase.buffer_length`, `ase.buffer_get_text`,
`ase.buffer_insert`, `ase.buffer_delete`, `ase.register_command` — enough
to write a real plugin (see the uppercase/reverse test fixtures) but
nothing about cursors, viewport, config, or editor chrome, none of which
exist as stable, plugin-safe surfaces yet. Extending the API means
adding another `l_*` binding function in `core/src/plugin_host.c` and
one more `lua_register` call — the shape doesn't change.

### 4. No command-to-keybinding wiring yet

Commands are invoked programmatically
(`ase_plugin_host_run_command(host, name, buffer)`) — there's no
keybinding config or command palette to trigger one from the running
GUI yet. That's real, deliberately out-of-scope follow-up work (Phase 4
shipped theme/font config, not keybindings — see `docs/ROADMAP.md`), not
a gap in the plugin system itself.

## Consequences

Adding a new extension point (e.g. a `register_command` for something
beyond buffer edits) means growing `AseCommandFn`'s signature or adding
a second command-kind — a real design decision to make deliberately
later, not implied by anything here. Native plugins are inherently
trust-the-file-you-installed; documented, not mitigated, in v1.
