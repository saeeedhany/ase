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
      Deferred out of this phase, tracked as follow-ups: line-index
      acceleration, periodic memory compaction, and CI fuzz integration
      (all called out in ADR 0005). Undo/redo — also called out there —
      landed as Phase 10 below.
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
- [x] **Phase 6 — LSP client module**
      `AseLspClient` in core (`core/include/ase/lsp_client.h`,
      `core/src/lsp_client.c`), on top of a new hand-rolled JSON module
      (`core/include/ase/json.h`, `core/src/json.c` — not a vendored
      library, see [ADR 0010](adr/0010-hand-rolled-json.md)). Spawns a
      language server as an isolated child process, speaks
      `Content-Length`-framed JSON-RPC over its stdio, does a bounded
      synchronous `initialize` handshake, then everything else
      (diagnostics via a standing callback, completion/definition via
      per-request callbacks) is non-blocking and timer-poll-driven, the
      same shape as config hot-reload. POSIX only in v1 — Windows
      `ase_lsp_client_start` returns `NULL` cleanly rather than shipping
      untested async-I/O code (`docs/adr/0011`, decision 6). Verified
      against a fake LSP server test fixture (deliberately independent
      of the real JSON module, so a shared bug can't hide symmetrically)
      covering the full lifecycle — handshake, `didOpen` → diagnostics,
      completion, definition, shutdown — plus a missing-executable case.
      All clean under ASan/UBSan. The JSON module alone also has 37.8M
      clean fuzz executions (`core/fuzz/fuzz_json.c`). See
      [ADR 0011](adr/0011-lsp-client.md) for the full design, including
      why `SIGPIPE` is now disabled process-wide and why server-to-client
      requests aren't answered.
      Not wired into the GUI yet — no diagnostics/completion/
      go-to-definition UI exists; tracked below, same pattern as
      Phase 5's plugin host.
- [x] **Phase 7 — Polish**
      All four items scoped and recorded in
      [ADR 0012](adr/0012-polish-phase-scope.md):
      - **Multi-cursor**: `EditorViewport` moved from one `size_t` cursor
        to `QVector<size_t> m_cursors`. New cursors via mouse click
        (new — there was no mouse handling before this phase),
        `Alt`+click, or `Ctrl+D` ("select next occurrence," Sublime/
        VS Code convention); `Escape` collapses back to one. Every edit
        applies at every cursor, processed highest-offset-first — proven
        safe without cross-cursor offset bookkeeping (ADR 0012, decision
        1). Verified live: clicking, `Alt`+clicking, and `Ctrl+D` each
        confirmed by typing afterward and screenshotting the same
        character landing at every active cursor simultaneously.
      - **Animation**: one, opt-in (`animations = true` in config,
        default `false`) — a smooth caret alpha-fade replacing the hard
        blink toggle. Verified live: sampled the caret's actual pixel
        color across three screenshots taken moments apart with
        animations on, confirming a continuously-varying alpha rather
        than a hard on/off value.
      - **Panel layout**: deliberately not built — there is, and has
        been through every phase, exactly one panel. Building a second
        one now just to have something to arrange would be unscoped
        scope creep; deferred until a real second panel exists (most
        plausibly an LSP diagnostics panel).
      - **Accessibility pass**: found and fixed a real issue —
        ADR 0007's dimmed comment color measured 3.64:1 contrast against
        the background, under WCAG AA's 4.5:1 for normal text; raised to
        4.91:1. Added accessible name/description. Full screen-reader
        text exposure (`QAccessibleInterface`) deliberately not
        attempted — unverifiable in this environment, and an untested
        implementation of an interface real assistive tech calls into
        risks being worse than the honest gap (same reasoning as
        ADR 0011's Windows decision). Tracked below.

## Current status

All seven phases from the spec's build order are done (see above).
Nothing left on the original phase list — remaining work is the
tracked follow-ups below (mostly: wiring the plugin host and LSP client
into the GUI) and the still-open license decision.

## Post-v1 fixes

- **Caret drift** (user-reported): the caret and syntax-highlight run
  positions were both computed as `column * m_charWidth`, an assumed
  fixed-pitch approximation that drifted visibly from the actually
  rendered text on long lines. Fixed by measuring actual rendered text
  width (`QFontMetrics::horizontalAdvance`) instead of assuming one.
  See [ADR 0013](adr/0013-caret-drift-fix.md).
- **Invisible caret** (user-reported, immediately after Phase 9): the
  disabled-animation branch of `updateAnimation` cleared
  `m_renderedCaretPos` every frame instead of populating it, and
  `paintEvent` only draws carets when that array is non-empty — so the
  caret never rendered at all under the default `animations = false`
  config. Fixed alongside a requested blink-mechanism change: the hard
  blink now resets on every cursor-moving action (stays solid visible
  throughout active use) and only resumes toggling ~500ms after input
  actually stops. See [ADR 0016](adr/0016-caret-visibility-fix.md) —
  note its "kept separate from the animated fade's phase" reasoning was
  superseded by ADR 0017 below, which found that separation was itself
  the cause of the fade still blinking during movement.
- **Viewport review pass** (user-reported, four issues at once): text
  rendered as blank space once a line scrolled far enough right
  (`width() - x` going negative in translated coordinates — the most
  severe of the four); the animated fade still blinked during movement
  (a free-running counter ADR 0016 didn't touch); typing/deleting fast
  felt laggy (the caret-glide easing couldn't keep pace with rapid
  small jumps and was never meant to try); and `QFontMetrics` was being
  reconstructed per styled run per frame instead of cached. All four
  fixed together — see [ADR 0017](adr/0017-viewport-review-pass.md).

## Post-v1 "feel alive" initiative

Direct feedback after v1: the editor works but doesn't feel alive
(abrupt caret, no view-follow, no line numbers). Phases 8–9 below
addressed that.

- [x] **Phase 8 — Viewport geometry**: horizontal scroll, two-axis
      view-follow, and an optional line-number gutter (`line_numbers`
      config key: `off`/`absolute`/`relative`, default `absolute`).
      See [ADR 0014](adr/0014-viewport-geometry.md).
- [x] **Phase 9 — Smooth motion**: caret glide + smooth scroll, one
      easing mechanism driving both, advanced from `paintEvent` itself
      (never stale relative to what's drawn). Fractional vertical
      scroll needed the one real new trick — render from
      `floor(renderedScrollLine)`, shift the whole line loop by the
      remainder, request one extra line at the bottom. Verified live:
      `animations = true` shows a large scroll jump visibly
      interpolating across frames (sampled gutter numbers converging
      232 → 237 → 238); `animations = false` produces pixel-identical
      consecutive frames after the same jump (true instant snap, no
      regression). See [ADR 0015](adr/0015-smooth-motion.md).

## Post-v1 "complete normal editor" initiative

Explicit direction: finish the editor as a complete *normal* text
editor first (undo, selection, clipboard, find/replace, chrome,
compile, LSP UI) — full modal Vim emulation is confirmed scope but
deliberately pushed to *after* this list, planned in detail only once
it's reached. Single file per window (no tabs) for now — multi-file
may become a tmux-like tiling plugin later. Full plan at the time of
writing in `~/.claude/plans/noble-herding-quokka.md`, phase-by-phase
ADRs as each lands here going forward.

- [x] **Phase 10 — Undo/redo**: `AseUndoStack` in core
      (`core/include/ase/undo.h`, `core/src/undo.c`) — records edit
      intent (offset + inserted/removed bytes), not snapshots, matching
      the diff-based design ADR 0005 anticipated. A whole multi-cursor
      keystroke undoes/redoes as one group, unwound in the exact
      reverse of application order. `Ctrl+Z`/`Ctrl+Shift+Z`. No
      coalescing of consecutive keystrokes (each is its own group) —
      deliberate v1 simplicity. See
      [ADR 0018](adr/0018-undo-redo.md).
- [ ] **Phase 11 — Selection model**: keyboard (Shift+arrows/Home/End)
      and mouse (click-drag, Shift+click) selection; currently there is
      no selection/anchor concept at all, only point cursors.
- [ ] **Phase 12 — Clipboard**: cut/copy/paste via `QClipboard`,
      built on Phase 11's selection.
- [ ] **Phase 13 — Find & replace**: `Ctrl+F`/`Ctrl+H`, plain substring
      match (no regex in v1), replace/replace-all as undo groups.
- [ ] **Phase 14 — Editor chrome**: status bar (line/col, dirty
      indicator — dirty tracking doesn't exist yet, ADR 0006 deferred
      it), `Ctrl+O` open / `Ctrl+Shift+S` save-as dialogs.
- [ ] **Phase 15 — Command line + `:compile` + output panel**: extracts
      a reusable `AseProcess` from the LSP client's process-spawning
      code; first real second panel.
- [ ] **Phase 16 — LSP diagnostics wiring**: `textDocument/didChange`
      (never sent today — results go stale after the first edit),
      GUI poll + diagnostic markers. `publishDiagnostics` parsing
      already exists in core and is tested; this is GUI wiring plus
      the one core addition.
- [ ] **Phase 17 — LSP completion + hover**: completion response
      parsing (currently handed back raw/unparsed) plus a popup;
      hover doesn't exist in the client at all yet, needs a new
      request/response pair added first.
- [ ] **Vim mode** (after Phase 17, unscoped until then): full modal
      emulation, not a lighter subset. Hard prerequisite (undo/redo)
      now satisfied by Phase 10; also benefits from Phase 11's
      selection model (Visual mode) and Phase 15's `:` command line
      (becomes the ex-command line).

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
- LSP client isn't wired into the GUI: no diagnostics rendering
  (squiggly underlines/gutter marks), no completion popup, no
  go-to-definition navigation. `AseLspClient` works standalone
  (ADR 0011) but nothing in `EditorViewport` calls into it yet.
- LSP client is POSIX-only — Windows support needs overlapped I/O or a
  reader thread (ADR 0011, decision 6), real work, not a quick add-on.
- No `textDocument/didChange` — the LSP client can tell a server a
  document was opened but not that it changed afterward.
- No panel layout infrastructure — there's exactly one panel; build
  this when a second one (e.g. LSP diagnostics) actually exists
  (ADR 0012, decision 3).
- No screen-reader text exposure (`QAccessibleInterface`) for the
  custom-painted viewport — a real, open accessibility gap, not
  attempted because it's unverifiable without a live AT-SPI client in
  this environment (ADR 0012, decision 4). Needs dedicated follow-up
  with proper assistive-technology test tooling.
- Vertical multi-cursor movement doesn't track a sticky column per
  cursor (only single-cursor mode does) — a minor, rare-in-practice
  paper cut (ADR 0012, decision 1).
- `Ctrl+D` "select next occurrence" doesn't wrap around the buffer.
