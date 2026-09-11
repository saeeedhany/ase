# Absolute Simple Editor

A minimal, robust, blazingly fast, and aesthetically deliberate GUI text editor.

Core and GUI are cleanly separated — the editing engine is a headless C library
(`core/`) with a stable C ABI, and the GUI (`gui/`) is a replaceable Qt6 shell on
top of it. See [ADR 0002](adr/0002-headless-core-separation.md) for why.

[:octicons-mark-github-16: Source on GitHub](https://github.com/saeeedhany/ase){ .md-button }
[:octicons-download-16: Latest release](https://github.com/saeeedhany/ase/releases/latest){ .md-button }

## Where to start

- **[Spec](SPEC.md)** — the original product spec: vision, design pillars,
  architecture, and build order. What this project is trying to be.
- **[Roadmap](ROADMAP.md)** — what's actually done, phase by phase, against
  that spec. What this project actually is, right now.
- **[Decisions](adr/index.md)** — every architecturally significant choice
  made along the way, in the order it was made, with the reasoning behind
  it and what it cost. The project's history, in its own words.
- **[Contributing](https://github.com/saeeedhany/ase/blob/main/CONTRIBUTING.md)**
  — ground rules, workflow, and the plugin-authoring guide.

## Status

First alpha release, `v0.1.0-alpha`, licensed under the
[Apache License 2.0](https://github.com/saeeedhany/ase/blob/main/LICENSE).
All seven original spec phases plus the full "complete normal editor" pass:
undo/redo, multi-cursor selection, clipboard, find/replace, viewport/scroll
polish, a floating command line with `:compile`, an LSP client (diagnostics,
completion, hover) wired into the GUI, and Linux packaging (AppImage, Arch
`PKGBUILD`, `.deb`). Vim mode is next — see the [roadmap](ROADMAP.md) for
everything else still open.
