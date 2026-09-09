# Roadmap

Tracks progress against the build order defined in [SPEC.md](SPEC.md#7-build-order--phases).
Each phase should land with tests/benchmarks before the next begins.

## Phases

- [x] **Phase 1 — Core buffer engine**
      Piece-table implementation (`core/src/buffer.c`), unit tests
      (`core/tests/test_buffer.c`), a benchmark tool
      (`core/bench/bench_buffer.cpp`, build with `-DASE_BUILD_BENCH=ON`),
      and a libFuzzer harness (`core/fuzz/fuzz_buffer.c`, build with
      `-DASE_BUILD_FUZZERS=ON` under clang — ~8.8M execs clean under
      ASan/UBSan locally, not yet wired into CI). No GUI dependency; see
      [ADR 0002](adr/0002-headless-core-separation.md) and
      [ADR 0005](adr/0005-buffer-engine-piece-table.md).
      Deferred out of this phase, tracked as follow-ups: undo/redo
      history, line-index acceleration, periodic memory
      compaction, and CI fuzz integration (all called out in ADR 0005).
- [x] **Phase 2 — Minimal Qt shell**
      `QMainWindow` + a custom-painted `EditorViewport` widget
      (`gui/src/editor_viewport.{h,cpp}`): renders the buffer with
      `QPainter` against the default palette, blinking caret, keyboard
      wired to `ase_buffer_insert/_delete` (typing, backspace/delete,
      arrow/Home/End navigation with a sticky column for up/down),
      wheel scrolling, `Ctrl+S` save / `Ctrl+Q` quit, no menu bar or
      other chrome. Verified by launching it against a real file,
      screenshotting the render, and confirming typed edits round-trip
      to disk via Ctrl+S. See
      [ADR 0006](adr/0006-gui-shell-v1-shortcuts.md) for this phase's
      deliberate shortcuts (full-buffer mirroring per keystroke,
      byte-level cursor, no IME, no dirty-tracking).
- [x] **Phase 3 — Syntax highlighting**
      Tree-sitter wired in via a new `ase_syntax` module
      (`modules/syntax/`): runtime + one grammar (C) fetched with CMake
      `FetchContent` and pinned to exact revisions, a hand-authored
      query (`src/queries/c_highlights.scm`), unit tests, and a
      libFuzzer harness on the parser boundary
      (`-DASE_BUILD_FUZZERS=ON`). Wired into `EditorViewport`: `.c`/`.h`
      files get highlighted, everything else renders plain. The default
      theme resolves the "one font color" pillar vs. "syntax
      highlighting" tension by varying only weight/style/opacity of the
      single text color (bold keywords, italic types, dimmed comments) —
      never a second hue. Verified by unit test and by screenshotting
      the editor highlighting its own `core/src/buffer.c`. See
      [ADR 0007](adr/0007-syntax-highlighting-tree-sitter.md) for all of
      this phase's decisions, including a measured (bounded, not
      leaking) memory characteristic of full-reparse-every-edit.
- [x] **Phase 4 — Theming & config system**
      `AseConfig` in core (`core/src/config.c`, `core/include/ase/config.h`):
      a minimal hand-rolled `key = value` parser (not TOML — see
      [ADR 0008](adr/0008-config-theme-format.md)) shared by config and
      theme. Ships built-in defaults, overlays a loaded file on top, and
      best-effort writes a commented starter file to
      `$XDG_CONFIG_HOME/ase/config.ase` (or platform equivalent) on
      first run. Wired into `EditorViewport`: background/text colors
      and font family/size all come from config now, and a 750ms poll
      hot-reloads on save — no restart. Verified with unit tests and by
      editing the live config file while the editor was running and
      screenshotting the color/font change landing without a restart.
      Resolves both ROADMAP open-decision items below (format, and the
      exact accent color — now a live-tunable value, not a hardcoded
      guess).
- [x] **Phase 5 — Plugin ABI + Lua scripting host**
      `AsePluginHost` in core (`core/include/ase/plugin_host.h`,
      `core/include/ase/plugin_abi.h`, `core/src/plugin_host.c`): one
      named-command registry populated two ways — Lua scripts (embedded
      Lua 5.4, fetched via `FetchContent` same as Tree-sitter) calling
      `ase.register_command`, or native `.so`/`.dylib`/`.dll` plugins
      exporting one `ase_plugin_register` symbol via `dlopen`. Both get
      a small buffer-editing API (`ase.buffer_*` for Lua,
      `AseBuffer*`-taking commands for native). Install-by-file, no
      registry/marketplace, per spec section 6. Verified with unit
      tests covering registration/replacement, missing-directory and
      broken-script handling (a syntax error in one script doesn't
      block others or crash the host), and an end-to-end test loading a
      real `.lua` fixture and a compiled native fixture from the same
      directory and running both against a buffer. All clean under
      ASan/UBSan (needed `CMAKE_POSITION_INDEPENDENT_CODE ON`
      project-wide once a static lib had to link into a dlopen'd
      module). See
      [ADR 0009](adr/0009-plugin-abi-and-lua-host.md) — including the
      correction of an earlier placeholder that had this living in
      `modules/plugins/` instead of core, per the spec's own
      architecture diagram.
      Not wired into the GUI yet — no keybinding/command-palette exists
      to trigger a registered command interactively; tracked below.
- [ ] **Phase 6 — LSP client module**
      Diagnostics, completion, go-to-definition.
- [ ] **Phase 7 — Polish**
      Multi-cursor, minimal/opt-in animations, panel layout, accessibility pass.

## Current status

Phases 1–5 are done (see above). Phase 6 (LSP client) has not started.

## Explicit non-goals for v1

- No built-in terminal emulator (defer to external terminal or a later plugin).
- No remote/SSH editing.
- No collaborative/multiplayer editing.
- No plugin marketplace/registry infrastructure — plugins are install-by-file.

## Open decisions blocking later phases

- **License** — permissive vs. copyleft, deliberately deferred.
  See [ADR 0003](adr/0003-license-decision-pending.md) (blocks going public).

## Follow-ups noted but not yet scheduled

- Syntax highlight capture styles (bold keyword, italic type, comment
  opacity — ADR 0007) are hardcoded in `EditorViewport`, not yet exposed
  as config keys.
- Keybinding customization isn't implemented — Phase 4 covered
  theme/editor config only, per its own scope in `docs/SPEC.md` section 7.
- No keybinding/command-palette wiring from the GUI to
  `ase_plugin_host_run_command` — plugins can register commands but
  nothing in the running editor triggers one interactively yet
  (ADR 0009, decision 4).
- Plugin directory isn't loaded automatically by the GUI at startup
  (unlike config, which is). Natural to add once there's a way to
  invoke a loaded command.
