# Absolute Simple Editor

A minimal, robust, blazingly fast, and aesthetically deliberate GUI text
editor. Core/GUI are cleanly separated so the engine can build and run
headless, with the GUI as a replaceable shell on top.

Full product spec: [`docs/SPEC.md`](docs/SPEC.md).
Current progress and open decisions: [`docs/ROADMAP.md`](docs/ROADMAP.md).
Why things are built the way they are: [`docs/adr/`](docs/adr/).

## Status

**Pre-Phase-1.** This is a scaffold: directory layout, build system, and
docs only. No buffer engine, no rendering, no editing yet. Not published
publicly — see [ADR 0003](docs/adr/0003-license-decision-pending.md), the
license is still an open decision.

## Architecture

```
┌───────────────────────────────────────────┐
│              GUI Shell (Qt6)               │
│  - window/menus/panels (native widgets)    │
│  - custom-painted text viewport            │
├───────────────────────────────────────────┤
│           Editor Core (C library)          │
│  - buffer engine (rope or piece table)     │
│  - undo/redo history                       │
│  - config & theme parser                   │
│  - plugin ABI + Lua scripting host         │
├───────────────────────────────────────────┤
│         Feature Modules (pluggable)        │
│  - Tree-sitter syntax highlighting         │
│  - LSP client (JSON-RPC over stdio)        │
│  - file tree / project explorer            │
│  - search & replace                        │
└───────────────────────────────────────────┘
```

The core (`core/`) never depends on the GUI (`gui/`); the GUI depends on the
core through a stable C ABI. See
[ADR 0002](docs/adr/0002-headless-core-separation.md).

## Building

Requires CMake ≥ 3.20 and a C11/C++20 compiler. Qt6 (Widgets) is required
only for the GUI target — if it isn't found, the GUI target is skipped and
the core still builds and tests cleanly.

```sh
cmake -B build -DASE_BUILD_GUI=ON -DASE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

Headless core-only build (no Qt required):

```sh
cmake -B build -DASE_BUILD_GUI=OFF
cmake --build build
ctest --test-dir build
```

Debug build with sanitizers on the core:

```sh
cmake -B build -DASE_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Layout

- `core/` — the engine: buffer, undo/redo, config/theme parsing, plugin ABI.
  Builds and runs headless.
- `gui/` — Qt6 shell: native chrome + custom-painted viewport.
- `modules/` — pluggable feature modules (syntax, LSP, plugins); empty
  placeholders until their respective phases (see `docs/ROADMAP.md`).
- `docs/` — spec, roadmap, and ADRs.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Note: external contributions
aren't being accepted yet while the license decision is open (ADR 0003).

## Design pillars

Minimal · Robust · Fast as hell · Aesthetic · Modular · Highly customizable ·
Maintainable & scalable · Open source. Details and rationale for each in
[`docs/SPEC.md`](docs/SPEC.md#2-design-pillars-non-negotiable).
