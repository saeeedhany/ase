# Contributing

**Status:** the license decision is still open
([ADR 0003](docs/adr/0003-license-decision-pending.md)), so this repo isn't
accepting external contributions yet. This document exists so the process
is ready once that's resolved.

## Ground rules

- Every architecturally significant change (new module boundary, new
  dependency, a change to the core/GUI contract, a data format decision)
  gets an ADR in `docs/adr/`, using `docs/adr/template.md`. Explain *why*,
  not just *what*.
- The core (`core/`) must keep building and passing its tests with
  `-DASE_BUILD_GUI=OFF` and no Qt installed. See
  [ADR 0002](docs/adr/0002-headless-core-separation.md). If a change makes
  that impossible, it's the wrong change or it belongs in `gui/`.
- No feature lands without earning its complexity budget — see the
  "Minimal" pillar in `docs/SPEC.md`. Prefer an opt-in plugin over adding
  to the core feature set.
- Sanitizers (ASan/UBSan) must stay clean on `core/`:
  `cmake -B build -DASE_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug`.

## Workflow

1. Open an issue describing the problem before writing code for anything
   beyond a trivial fix.
2. Keep PRs scoped to one phase/concern from `docs/ROADMAP.md` where
   possible.
3. Add or update tests for `core/` changes; add a benchmark if the change
   touches the buffer engine's performance characteristics.
4. CI must pass, including the sanitizer job, before merge.

## Plugin authoring guide

Plugins are install-by-file (no marketplace/registry in v1): drop a
`.lua` script or a compiled `.so`/`.dylib`/`.dll` into a plugins
directory, and `ase_plugin_host_load_directory` (`core/include/ase/plugin_host.h`)
loads it. Both kinds register into the same named-command registry —
see [ADR 0009](docs/adr/0009-plugin-abi-and-lua-host.md) for the full
design rationale.

**Lua plugins** get one global table, `ase`, with:

- `ase.register_command(name, function(buffer) ... end)`
- `ase.buffer_length(buffer)`, `ase.buffer_get_text(buffer, at, len)`,
  `ase.buffer_insert(buffer, at, text)`, `ase.buffer_delete(buffer, at, len)`

```lua
-- uppercase.lua
ase.register_command("uppercase_all", function(buf)
    local len = ase.buffer_length(buf)
    local text = ase.buffer_get_text(buf, 0, len)
    ase.buffer_delete(buf, 0, len)
    ase.buffer_insert(buf, 0, string.upper(text))
end)
```

**Native plugins** export exactly one C symbol:

```c
#include "ase/plugin_abi.h"

static void my_command(AseBuffer *buffer, void *user_data) { /* ... */ }

void ase_plugin_register(AsePluginHost *host, const AsePluginApi *api) {
    api->register_command(host, "my_command", my_command, NULL);
}
```

A crashing native plugin crashes the editor — that's an accepted,
inherent cost of native (as opposed to Lua) plugins, not something this
layer sandboxes against. See `core/tests/fixtures/` for complete working
examples of both, and `core/tests/test_plugin_host.c` for how they're
loaded and invoked.

There's no keybinding or command-palette wiring yet to trigger a
registered command from the running GUI — commands are invoked
programmatically (`ase_plugin_host_run_command`) for now. Tracked as a
follow-up in `docs/ROADMAP.md`.
