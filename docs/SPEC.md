# Project Specification: Absolute Simple Editor

## 1. Vision

A minimal, robust, blazingly fast, and aesthetically deliberate GUI text editor.
Built as an open-source, modular, highly customizable system with a clean
core/GUI separation — engineered for long-term maintainability and scale,
not a weekend prototype.

The product philosophy: **the editor gets out of the way**. No feature exists
unless it earns its complexity budget. Every subsystem is replaceable without
touching the others.

## 2. Design Pillars (non-negotiable)

| Pillar | Meaning in practice |
|---|---|
| **Minimal** | No unnecessary chrome, no bloated default feature set. Power comes from opt-in plugins, not a kitchen-sink core. |
| **Robust** | Never loses user data. Crash-safe autosave/journaling. Extensive sanitizer/fuzz testing on the core buffer engine. |
| **Fast as hell** | Sub-frame (<16ms, ideally <4ms) paint times on typical files. Instant startup (<100ms cold). No perceptible input latency, ever. |
| **Aesthetic** | Extreme color minimalism: one background color, one font color, theme-driven and derived from the background so the palette stays coherent. Default palette: background `#282828`, text a warm off-white with a slight yellow tint (approx. `#F5E6C8` — exact value TBD, to be tuned by eye). Text is the sole focus of the screen. Everything else — line numbers, bracket/indent guide lines, gutters, any secondary UI element — is visually de-emphasized, not competing for attention. No visible settings/help/menu chrome; those are accessed via shortcuts, not on-screen elements. |
| **Modular** | Core engine, GUI shell, syntax layer, LSP client, plugin host are independently buildable/replaceable components. |
| **Highly customizable** | Theming, keybindings, and behavior fully user-configurable via plain-text config, with sane, opinionated defaults out of the box. |
| **Maintainable & scalable** | Clean C/C++ API boundaries, documented architecture decisions, CI-verified on every change, designed so new contributors can extend it without reading the whole codebase. |
| **Open source** | Public repo, permissive or copyleft license (decide deliberately), contribution guide, and documentation treated as a deliverable, not an afterthought. |

## 3. Architecture Overview

```
┌───────────────────────────────────────────┐
│              GUI Shell (Qt6)               │
│  - window/menus/panels (native widgets)    │
│  - custom-painted text viewport            │
│    (QPainter/QOpenGLWidget/QRhi)           │
├───────────────────────────────────────────┤
│           Editor Core (C/C++ lib)          │
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

**Rule:** the core library must build and run headless (no GUI dependency).
This forces real separation and enables future frontends (TUI, embeddable
widget, etc.) without rewriting the engine.

## 4. Technology Choices

- **Core language:** C, or C with a thin C++ wrapper for RAII/containers.
- **Buffer data structure:** rope (preferred for scale) or piece table
  (simpler undo/redo semantics). Decision made after benchmarking both
  against representative large-file workloads.
- **GUI toolkit:** Qt6. Native widgets for chrome (menus, dialogs, panels);
  fully custom-painted viewport for the text area itself — bypassing
  `QTextEdit` entirely for performance.
- **Rendering path:** `QPainter` initially; `QRhi`-backed GPU rendering as
  a later optimization pass if profiling shows it's warranted.
- **Syntax highlighting:** Tree-sitter (incremental parsing, wide grammar
  ecosystem).
- **Language intelligence:** LSP client module, isolated process boundary
  via JSON-RPC over stdio — a misbehaving language server must never crash
  the editor.
- **Extensibility:** Lua scripting host for user-facing plugins/config
  logic; stable C ABI for compiled feature modules.
- **Config & themes:** plain-text (TOML or minimal custom format),
  hot-reloadable, single format shared between config and theme files.

## 5. Non-Functional Requirements

- **Startup time:** cold start under 100ms on typical hardware.
- **Input latency:** keystroke-to-pixel under one frame at 60Hz; no
  measurable input lag on files up to several hundred MB.
- **Memory:** proportional to file size, not a fixed multiple that breaks
  on large files — rope/piece-table choice is driven by this.
- **Crash safety:** periodic autosave/journal; on relaunch after a crash,
  no more than a few seconds of edits should be unrecoverable.
- **Portability:** Linux first-class; macOS/Windows supported via Qt's
  cross-platform layer.
- **Testing:** unit tests on the core buffer engine, fuzz testing on parser
  boundaries, AddressSanitizer/UndefinedBehaviorSanitizer in CI.

## 6. Explicit Scope Boundaries (v1)

State plainly what is deliberately out of scope early on, and flag these in
code comments/docs wherever a shortcut was taken for this reason:

- No built-in terminal emulator in v1 (defer to external terminal or a
  later plugin).
- No remote/SSH editing in v1.
- No collaborative/multiplayer editing in v1.
- No plugin marketplace/registry infrastructure in v1 — plugins are
  install-by-file initially.

## 7. Build Order / Phases

1. **Core buffer engine** — rope or piece-table implementation, unit tests,
   benchmarks. No GUI dependency at all.
2. **Minimal Qt shell** — window, custom-painted viewport rendering the
   buffer, keyboard input wired to core edit operations.
3. **Syntax highlighting** — Tree-sitter integration into the viewport
   render path.
4. **Theming & config system** — parser, hot-reload, default theme(s)
   matching the project's visual identity.
5. **Plugin ABI + Lua scripting host** — stable extension surface.
6. **LSP client module** — diagnostics, completion, go-to-definition.
7. **Polish** — multi-cursor, minimal/opt-in animations, panel layout,
   accessibility pass.

## 8. Maintainability & Documentation Requirements

- Doxygen-generated API reference for the core C/C++ surface.
- Architecture Decision Records (ADRs) checked into the repo from the
  first commit, documenting *why* each major structural choice was made.
- `CONTRIBUTING.md` including a plugin-authoring guide.
- CI matrix across Linux/macOS/Windows, running sanitizers on every PR.
- README covering build instructions, architecture summary, and a
  quickstart for both users and contributors.

## 9. Open Source Logistics

- License: TBD — deliberate choice between permissive (MIT/Apache-2.0) and
  copyleft (GPL/LGPL), based on desired plugin ecosystem dynamics.
- Public repository with issue templates and a clear roadmap doc.
- Versioning scheme and changelog discipline defined before v1.0.
