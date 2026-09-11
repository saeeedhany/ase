<div align="center">

<img src="assets/readme-banner.png" alt="Absolute Simple Editor" width="720">

A minimal, robust, blazingly fast, and aesthetically deliberate GUI text editor.

[![Release](https://img.shields.io/github/v/release/saeeedhany/ase?include_prereleases&style=flat-square&color=689d6a)](https://github.com/saeeedhany/ase/releases/latest) [![CI](https://img.shields.io/github/actions/workflow/status/saeeedhany/ase/ci.yml?branch=main&style=flat-square&label=CI)](https://github.com/saeeedhany/ase/actions/workflows/ci.yml) [![License](https://img.shields.io/github/license/saeeedhany/ase?style=flat-square&color=689d6a)](LICENSE) [![Docs](https://img.shields.io/badge/docs-mkdocs-689d6a?style=flat-square)](https://saeeedhany.github.io/ase/) [![Discord](https://img.shields.io/badge/chat-discord-689d6a?style=flat-square&logo=discord&logoColor=white)](https://discord.gg/sBkH45DzHc)

</div>

Core/GUI are cleanly separated so the engine can build and run headless,
with the GUI as a replaceable shell on top.

**[Browse the docs site](https://saeeedhany.github.io/ase/)** — spec, roadmap,
and the full architecture-decision history, or read the source directly:
[`docs/SPEC.md`](docs/SPEC.md), [`docs/ROADMAP.md`](docs/ROADMAP.md),
[`docs/adr/`](docs/adr/). The site is built with MkDocs
(`docs/requirements.txt` + `mkdocs.yml`) — `mkdocs serve` for a local
preview, `mkdocs gh-deploy` to publish; see
[ADR 0035](docs/adr/0035-mkdocs-documentation-site.md) for why.

## Status

**First alpha release (v0.1.0-alpha).** All seven original spec phases
are done, plus the full "complete normal editor" pass on top: undo/
redo, multi-cursor selection, clipboard, find/replace, viewport/scroll
polish, a floating command line with `:compile`, and an LSP client
(process-isolated, diagnostics + completion + hover, POSIX only so
far) wired directly into the GUI — squiggle/gutter diagnostics, an
automatic completion popup, and mouse-hover info, all driven by
`lsp_command` in `config.ase`. See [`docs/ROADMAP.md`](docs/ROADMAP.md)
for the full phase-by-phase history and what's still open (Vim mode is
next). The plugin host (Lua + native `dlopen` plugins) still has no
keybinding/command-palette wiring in the GUI — commands run
programmatically only for now.

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
the core still builds and tests cleanly. `core/` itself fetches Lua at
configure time (needs network access on a clean build), and the syntax
module (`-DASE_BUILD_SYNTAX=ON`, default) likewise fetches Tree-sitter
and its C grammar — both cached after the first configure.
The GUI target depends on the syntax module (`-DASE_BUILD_GUI=ON`
requires `-DASE_BUILD_SYNTAX=ON`); disabling syntax while keeping the GUI on is
not a supported combination.

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

Prebuilt packages (AppImage, Arch `PKGBUILD`, `.deb`) are documented in
[`packaging/README.md`](packaging/README.md); the alpha's own build of
each is attached to the
[latest release](https://github.com/saeeedhany/ase/releases/latest).

## Layout

- `core/` — the engine: buffer, config/theme parsing, undo/redo, plugin
  ABI + Lua host, JSON + LSP client. Builds and runs headless.
- `gui/` — Qt6 shell: native chrome + custom-painted, multi-cursor viewport.
- `modules/syntax/` — Tree-sitter syntax highlighting (the only feature
  module built so far — LSP and the plugin host both ended up living in
  `core/` instead; see `modules/README.md`).
- `packaging/` — AppImage, Arch `PKGBUILD`, and `.deb` build scripts.
- `docs/` — spec, roadmap, ADRs, and the MkDocs site source.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md).

## License

Apache License 2.0 — see [`LICENSE`](LICENSE) and
[`NOTICE`](NOTICE). See
[ADR 0033](docs/adr/0033-license-apache-2.0-and-first-alpha-release.md)
for why.

## Design pillars

Minimal · Robust · Fast as hell · Aesthetic · Modular · Highly customizable ·
Maintainable & scalable · Open source. Details and rationale for each in
[`docs/SPEC.md`](docs/SPEC.md#2-design-pillars-non-negotiable).
