# Roadmap

Tracks progress against the build order defined in [SPEC.md](SPEC.md#7-build-order-phases).
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

All seven phases from the spec's build order are done (see above), and
so is everything the "wiring" note below used to point at: the plugin
host and the LSP client are both in the GUI, and the license decision
was settled by [ADR 0033](adr/0033-license-apache-2.0-and-first-alpha-release.md).

At `v0.4.0-beta` the editor has a Vim mode pinned by 218 conformance
cases generated from real vim, Tree-sitter highlighting for seven
languages, an LSP client with diagnostics/completion/hover/go-to-
definition sharing one server per language per project, built-in
palettes, keybindings as data, session restore, git gutter marks, an
atomic save and crash recovery for unsaved work.

What is still open is in *Beyond v1* below, which has been pruned of
everything since finished.

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
- **LSP silently not working with a relative path** (user-reported,
  after Phase 17): launching as `ase_gui file.c` (relative, the common
  case) built the LSP document URI straight from the unresolved
  argument via `QUrl::fromLocalFile`, which expects an absolute path —
  a real server (`clangd`) rejected every message outright
  ("unresolvable URI"), silently disabling diagnostics, completion,
  and hover with no visible error in the editor itself. Fixed by
  resolving through `QFileInfo::absoluteFilePath` first. Verified live
  by reproducing the report's exact launch shape (relative path,
  matching working directory) and confirming a clean `clangd` round
  trip. See [ADR 0032](adr/0032-lsp-relative-path-uri-fix.md).

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
may become a tmux-like tiling plugin later. The full plan was kept
outside this repo at the time of writing; phase-by-phase ADRs land
here as each is reached.

- [x] **Phase 10 — Undo/redo**: `AseUndoStack` in core
      (`core/include/ase/undo.h`, `core/src/undo.c`) — records edit
      intent (offset + inserted/removed bytes), not snapshots, matching
      the diff-based design ADR 0005 anticipated. A whole multi-cursor
      keystroke undoes/redoes as one group, unwound in the exact
      reverse of application order. `Ctrl+Z`/`Ctrl+Shift+Z`. No
      coalescing of consecutive keystrokes (each is its own group) —
      deliberate v1 simplicity. See
      [ADR 0018](adr/0018-undo-redo.md).
- [x] **Phase 11 — Selection model**: keyboard (Shift+arrows/Home/End)
      and mouse (click-drag, Shift+click) selection. New
      `m_selectionAnchors`, index-aligned with `m_cursors`
      (`m_selectionAnchors[i] == m_cursors[i]` means no active
      selection) — same pattern `m_renderedCaretPos` already used.
      Every move op takes an `extend` flag (Shift held): extending
      moves the head and leaves the anchor fixed; a plain move with an
      active selection collapses to the near/far edge instead of
      stepping, matching standard editor convention. Typing/Backspace/
      Delete over a selection replace/remove exactly that range, as
      part of the same undo group (Phase 10). New translucent
      `selection` config color, painted in its own pass ahead of the
      text so glyphs stay crisp on top. Verified live via `xdotool` +
      screenshots (keyboard extend/collapse) and a save-to-file round
      trip (type-over-selection, undo). See
      [ADR 0019](adr/0019-selection-model.md).
- [x] **Phase 12 — Clipboard**: cut/copy/paste via `QClipboard`, built
      on Phase 11's selection. `Ctrl+C` reads every selected cursor's
      text out of `m_cache` and joins multiple selections with `\n`;
      `Ctrl+X` does the same then deletes them as one undo group;
      `Ctrl+V` inserts the clipboard text at every cursor, reusing
      `insertText`'s existing selection-replace and multi-cursor
      broadcast. No `CMakeLists.txt` change needed — `Qt6::Widgets`
      already pulls in `Qt6::Gui`. Verified live against the real
      system clipboard via `xclip` (independent of the Qt process), a
      save-to-file round trip for paste, and a cut-then-`Ctrl+Z`
      round trip. See [ADR 0020](adr/0020-clipboard.md).
- [x] **Phase 13 — Find & replace**: `Ctrl+F`/`Ctrl+H` open a new
      keyboard-only `FindBar` (`gui/src/find_bar.{h,cpp}`). Plain
      substring, ASCII-case-insensitive, no regex. The current match is
      just an active selection (`jumpToMatch` sets `m_cursors`/
      `m_selectionAnchors` to it), so `Enter`-to-replace reuses
      Phase 11's selection-replace path unchanged; Replace All
      (`Ctrl+Enter`) writes highest-offset-first as one undo group,
      the same discipline every other multi-offset edit in this file
      follows. Verified live via `xdotool` — pixel-level highlight
      checks and save-to-file round trips for replace-one/replace-all,
      each undoing as one action. See
      [ADR 0021](adr/0021-find-replace.md). **Restyled immediately
      after** — see Phase 13.5 below; `FindBar` now floats on a
      `FloatingPanel` base rather than the docked bar this phase
      originally shipped.
- [x] **Phase 13.5 — Floating-panel design system**: direct feedback
      that the docked find bar "looks so basic," plus the knowledge
      that Open/Save-As panels are coming next, prompted a real reusable
      system instead of another one-off shape. New
      `FloatingPanel` (`gui/src/floating_panel.{h,cpp}`) — a
      self-centering, flat, translucent, fade-in/out child widget any
      future chrome (Open/Save-As, etc.) can build on — and
      `LetterBadge` (`gui/src/letter_badge.{h,cpp}`) — a small
      single-letter chip replacing text labels ("F"/"R" today). Two
      decisions taken to the user rather than guessed: panels are
      **true-centered** on the window (not top-anchored), and future
      Open/Save-As will be **custom floating panels**, not native
      `QFileDialog` — overriding what the original Phase 14 plan
      sketched; that plan needs a matching update before Phase 14
      starts. `main.cpp` reverted to its pre-Phase-13
      simplicity (`FindBar` is a child of `EditorViewport`, not a
      layout row). See [ADR 0022](adr/0022-floating-panel-design-system.md).
- [x] **Phase 14 — Editor chrome**: status bar (`Ln %1, Col %2` + a
      `*` when dirty), dirty tracking (`m_dirty`, set by every
      mutating op including undo/redo, cleared by a successful save —
      ADR 0006 had deferred this), and `Ctrl+O`/`Ctrl+Shift+S` via a
      new `FileBrowserPanel` (`gui/src/file_browser_panel.{h,cpp}`) —
      a second `FloatingPanel`, per [ADR 0022](adr/0022-floating-panel-design-system.md)'s
      decision to use custom panels instead of native `QFileDialog`.
      `EditorViewport` got its first `Q_OBJECT`/signal
      (`statusChanged`), emitted from `ensureCursorVisible()` — the one
      choke point every cursor move and every edit already passes
      through. `Ctrl+S` with no path set now opens Save-As instead of
      silently doing nothing. A real bug found and fixed during live
      verification: both the file browser's path field and its list
      needed `Return` consumed via `eventFilter` rather than left to
      Qt's native `returnPressed`/`itemActivated` — a focus change
      inside the handler was causing the same key press to be
      redelivered to the freshly-focused editor afterward. See
      [ADR 0023](adr/0023-editor-chrome.md).
- [x] **Phase 14.5 — Panel polish, scroll margin, status bar theme**:
      direct feedback that `FileBrowserPanel`'s close animation and
      navigation "felt unsmooth," plus a request to search by name
      instead of editing a path, and a scrolloff-style viewport margin.
      Root-caused the animation jank to `FloatingPanel` animating its
      own real `geometry` (forcing a live `QVBoxLayout`/`QListWidget`
      relayout every frame) — reworked to animate a static snapshot
      image instead (`contentWidget()`, `m_snapshot`), which also
      surfaced and fixed a genuine "geometry set while hidden doesn't
      survive the child's first show()" class of bug. The file
      browser's path field is now a filter (placeholder shows just the
      directory name, typing filters the list and auto-selects the
      first match) with a custom animated sliding highlight bar — which
      itself uncovered a real, previously-invisible bug: `..` was
      being fully hidden by an opaque highlight (`QPalette`/
      `setAutoFillBackground` silently ignores alpha), fixed with a
      small `TranslucentBar` that paints via `QPainter::fillRect`
      directly, same technique the editor's own selection/find-match
      overlays already use. `ensureCursorVisible()` now keeps a
      3-line/4-character margin around the cursor before scrolling.
      Status bar themed to match the app (`QPalette`, re-applied on
      every `statusChanged`). See
      [ADR 0024](adr/0024-panel-polish-and-scroll-margin.md).
- [x] **Phase 15 — Command line + `:compile` + output panel**: new
      `AseProcess` (`core/include/ase/process.h`,
      `core/src/process.c`) extracted from the LSP client's process-
      spawning code — `lsp_client.c` is now a consumer of it, verified
      by its existing test suite still passing unchanged; new
      `core/tests/test_process.c` covers the module directly. A key
      opens a `CommandLine` `FloatingPanel` (`gui/src/command_line.{h,cpp}`,
      smallest of the family — one badge, one field); `:w`/`:q`/
      `:compile`/`:output` dispatched by
      `EditorViewport::runCommand`, unrecognized input a silent no-op.
      `:compile` reads `build_command` from config (no default),
      substitutes `%f`, runs it through a shell with the file's
      directory as `cwd`, and streams output into a new `OutputPanel`
      (`gui/src/output_panel.{h,cpp}`) — this app's first *docked*
      panel rather than a floating one, a deliberate choice confirmed
      with the user: you want to watch a build stream while still
      looking at your code, which a centered overlay would fight.
      Verified live via `xdotool`: a real `build_command` streams its
      output and exit code correctly. See
      [ADR 0025](adr/0025-command-system.md). **The trigger key
      changed immediately after** — see Phase 15.5 below; it's
      `Ctrl+;` now, not a bare `:`.
- [x] **Phase 15.5 — Keybinding fix, Help/About panels, find bar
      repositioned**: a real bug — the bare `:` binding meant `:` could
      never be typed as a literal character — fixed by moving the
      command line to `Ctrl+;` (`:` is a Vim ex-command-line
      convention this non-modal editor shouldn't have claimed).
      `Ctrl+B` compiles directly, `Ctrl+Shift+O` toggles the output
      panel directly, both alongside (not replacing) their `:compile`/
      `:output` command-line equivalents. New `HelpPanel` (`Ctrl+/`,
      `gui/src/help_panel.{h,cpp}`) and `AboutPanel` (`Ctrl+I`,
      `gui/src/about_panel.{h,cpp}`), both centered `FloatingPanel`s —
      Help is the family's first panel with no input field (a
      scrollable keybinding reference instead), About shows app
      name/version/author and clickable GitHub/website links. New
      `FloatingPanel::Anchor` (`Center`/`TopRight`) lets one panel
      override its position — `FindBar` is the only one that does,
      moved top-right per direct feedback that a centered find bar
      sits on top of the text you're actively searching. See
      [ADR 0026](adr/0026-keybinding-scheme-help-about.md).
- [x] **Phase 15.6 — Logo, title-color bug, thin scrollbars, faster
      animation, output divider**: the app's logo
      (`gui/resources/ase.png`, bundled via Qt resources/`.qrc`) is now
      the window icon and appears in the About panel. Fixed a real
      bug: Help/About panel titles rendered black, not themed — the
      one `QLabel` each panel's `refreshTheme()` had missed. New
      shared `thinScrollBarStyleSheet` (`gui/src/scrollbar_style.{h,cpp}`)
      — thin, thickens on hover — applied to Help, the output panel,
      and the file browser's list. Enter now glides the caret to the
      new line (previously snapped like any other edit); every
      animation duration (caret glide/fade, blink, panel pop, row
      highlight) sped up. `TranslucentBar` extracted to its own file
      (`gui/src/translucent_bar.{h,cpp}`) and reused for a small,
      centered seam marker at the output panel's top edge — found and
      fixed a second real bug along the way: the output panel itself
      had no themed background, so the area around that marker
      rendered Qt's default light gray. See
      [ADR 0027](adr/0027-visual-polish-pass.md).
- [x] **Phase 15.7 — Click precision fix, deletion glide, select-all,
      About centering, synced input blink**: fixed a real, reported
      bug — clicking to place the cursor rounded down to the nearest
      column's left edge instead of the nearest column, so a click in
      a character's right half needed the pointer to move almost a
      full character further right than expected before the cursor
      caught up; verified fixed via a systematic click sequence.
      Backspace/Delete now glide like Enter does (ADR 0027) rather
      than snapping. New `Ctrl+A` select-all. `AboutPanel`'s body text
      now centers under its logo instead of reading left-aligned
      beneath a centered image. `QApplication::setCursorFlashTime`
      syncs every floating panel's native input-field caret to the
      editor's own blink rate (a partial answer to "make the caret
      animation global" — Qt's native caret can't do the smooth fade,
      only match the rate). See
      [ADR 0028](adr/0028-click-precision-select-all-deletion-glide.md).
- [x] **Phase 16 — LSP diagnostics wiring**: new `ase_lsp_client_did_change`
      (full-document sync, called from `refreshCache()` so diagnostics
      never go stale after an edit), a 200ms GUI poll timer, and
      rendering — a wavy squiggle under each diagnostic's range plus a
      severity-colored gutter dot, both driven by two new config keys
      (`diagnostic_error`/`diagnostic_warning` — a deliberate, documented
      exception to the one-font-color pillar) and a third,
      `lsp_command`, following `build_command`'s "no default, don't
      guess" pattern. Found and fixed a real bug along the way: the
      shared `AseProcess` spawn helper always merged a child's stderr
      into its stdout pipe, which is fine for `:compile` and for the
      test fixture but silently corrupted the framed JSON-RPC stream
      against a real server (`clangd` logs to stderr) — split into
      `ase_process_spawn` (unchanged, merged) and a new
      `ase_process_spawn_ex(..., merge_stderr)` the LSP client calls
      with `false`. Verified live against real `clangd`: a missing-
      semicolon error produced a visible squiggle + gutter dot, and
      retyping the fix cleared both without restarting the editor. See
      [ADR 0029](adr/0029-lsp-diagnostics-wiring.md).
- [x] **Phase 17 — LSP completion + hover**: new
      `ase_lsp_client_request_hover` in core (didn't exist before this
      phase); GUI-side completion is automatic while typing (gated to
      "right after an identifier char or a member-access trigger," not
      every keystroke) via a new custom-painted `CompletionPopup`,
      accepted with Enter/Tab by replacing the typed prefix in place;
      hover is automatic on a ~500ms mouse pause via a new `HoverPanel`,
      dismissed on move-away/click/scroll/key/focus-loss. Both are
      deliberately not `FloatingPanel`s — they track a moving point and
      refresh far more often than a glance-act-dismiss chrome window,
      so each only fades (no scale-pop) and only on first appearance,
      not on every in-place content refresh. Verified live against real
      `clangd`: a completion popup with real macro suggestions,
      correct prefix-replace on accept with no reopen-loop; a hover
      tooltip showing clangd's real type/value/declaration info,
      dismissing correctly on mouse-away. See
      [ADR 0030](adr/0030-lsp-completion-hover.md).
- [x] **Phase 17.5 — Draggable panels, smooth panel scroll, tracking-
      popup glide, Tab**: user feedback framed as "finish the base
      editor" rather than a new feature. `FloatingPanel::setDragHandle`
      (one call per panel, using the badge every panel already has) —
      every existing panel is now draggable, and any future one is too
      with the same one line. `installSmoothScroll()`, a free function
      working on any `QAbstractScrollArea` (`QScrollArea`,
      `QListWidget`, `QPlainTextEdit` all qualify), eases wheel-scroll
      instead of jumping — applied to Help, File browser, and Output.
      New shared `TrackingPopup` base for `CompletionPopup`/`HoverPanel`
      (Phase 17): both now glide their *position* toward a moving
      target (the caret while typing, the pointer while still hovering
      the same word) instead of jumping, and any future tracking
      overlay gets the same fade/paint/glide identity for free by
      deriving from it. Tab now inserts a soft (4-space) indent — a
      real, reported gap (it previously did nothing at all). Checked,
      not just fixed: FindBar's open/close/resize animation was
      confirmed (via a temporary debug print) to already fire
      identically to every other panel — nothing was actually broken
      there. See
      [ADR 0031](adr/0031-draggable-panels-smooth-scroll-tracking-glide.md).
- [x] **License & first alpha release**: Apache License 2.0
      (`LICENSE`/`NOTICE`), version bumped to `0.1.0-alpha`
      (`ASE_VERSION_STRING`, a single source of truth read by the About
      panel), tagged `v0.1.0-alpha`, pushed to
      [github.com/saeeedhany/ase](https://github.com/saeeedhany/ase).
      See [ADR 0033](adr/0033-license-apache-2.0-and-first-alpha-release.md).
- [x] **Linux packaging**: `install()` rules (`gui/CMakeLists.txt`) as
      the one foundation every format below builds on — a `.desktop`
      file and a new padded-square icon alongside the existing wordmark
      logo. AppImage (`packaging/appimage/`, via linuxdeploy — two real
      bugs found and fixed while actually building one: wrong Qt
      version auto-detected, and linuxdeploy's bundled `strip` choking
      on a newer ELF section), an Arch `PKGBUILD` (`packaging/arch/`,
      building from the real GitHub release tag — verified with a real
      `makepkg -f` run, not just written), and a dependency-scanned
      `.deb` (`packaging/debian/`, built and its payload run standalone
      to confirm it actually works, not just that `dpkg-deb` didn't
      error). See [ADR 0034](adr/0034-linux-packaging.md).
- [x] **Documentation site**: MkDocs + Material, deployed to GitHub
      Pages at [saeeedhany.github.io/ase](https://saeeedhany.github.io/ase/)
      — the same `docs/SPEC.md`/`docs/ROADMAP.md`/`docs/adr/*.md` this
      repo already had, now browsable with search and a real nav
      instead of only readable file-by-file on GitHub. New
      `docs/adr/index.md`, a generated table of every decision in
      order, is the "project history" view this was built for. Two
      real rendering bugs (missing markdown extensions for buttons/
      icons; the dev server's `/ase/` path prefix) found by actually
      loading the built site in a browser, not just a clean
      `mkdocs build --strict`. See
      [ADR 0035](adr/0035-mkdocs-documentation-site.md). Rebranded
      right after (user-reported: it still showed Material's default
      logo and indigo color scheme) — the actual "ase" wordmark as
      logo/favicon, and a custom palette matching the editor's own
      `#282828`/`#F5E6C8`, plus a new docs-only accent `#689d6a` (the
      editor has no general-purpose accent color of its own to reuse
      — see ADR 0007). A placeholder `docs/plugins.md` page was also
      added. See
      [ADR 0036](adr/0036-docs-site-theme-and-plugins-page.md).
- [x] **Feedback triage log**: `docs/feedback/`, structured the same
      way as `docs/adr/` (numbered batch files, an index, an explicit
      nav entry each) — a formal, versioned response to external
      review comments, attributed to the reporter and linked back to
      the source, distinct from the live conversation on Discord/
      GitHub issues where a report first lands. First batch: seven
      comments from the alpha release's first external review
      (`@TOTO-sys28` on Discord) — a CMake robustness gap and a
      vendored-Lua compiler warning are fixed; two real packaging
      version-floor bugs (`.deb`/AppImage built on a rolling-release
      dev machine instead of an old, pinned base) remain open, to be
      done together with a future batch of fixes rather than as an
      isolated rebuild; the cramped Help panel is deferred to a future
      system-wide floating-panel design pass rather than an isolated
      layout patch (see [ADR 0038](adr/0038-feedback-triage-criteria-and-deferred-status.md),
      which also writes down the criteria for telling a real defect
      apart from a design choice, and adds the `Deferred` status this
      uses); two items checked and found not to be current bugs. See
      [ADR 0037](adr/0037-feedback-triage-log.md).
- [x] **Mouse hit-test drift fix**: user-reported bug — clicking or
      hovering further right on a line with mixed syntax-highlight
      styling (e.g. bold keywords next to plain identifiers)
      increasingly missed the character under the pointer. Same root
      cause as ADR 0013's caret-drift bug (a fixed per-character pixel
      width instead of measuring what was actually rendered), just
      never fixed on the mouse side. New `columnForX()` mirrors
      `xForColumn()`'s real per-run measurement; verified live by
      confirming a mouse click and a keyboard-placed caret at the same
      pixel now resolve to the same character, deep into a styled
      line. See [ADR 0039](adr/0039-mouse-hit-test-drift-fix.md).
- [x] **Italic glyph clipping fix**: user-reported bug — italic `void`
      was visibly misreadable as `voia`, the ascender of the italic
      `d` sheared off. `drawText`'s implicit per-run clip rect was
      sized from the plain font's metrics, too tight for an italic
      variant's taller ascent. Fixed with `Qt::TextDontClip`; verified
      live with a zoomed screenshot. See
      [ADR 0040](adr/0040-italic-glyph-clipping-fix.md).
- [x] **Diagnostic underline redesign**: the wavy zigzag underline is
      now a plain thin line, dim by default and brightening to full
      opacity with a fast, smooth left-to-right wipe on the line the
      cursor is on (reversing when it leaves) — the gutter dot gets the
      same dim/focus distinction as a plain fade, no wipe. Verified
      live against real clangd diagnostics, including catching the
      wipe and its reverse mid-animation. See
      [ADR 0041](adr/0041-diagnostic-underline-and-dot-focus.md).
- [x] **Empty-buffer welcome overlay**: the "ase" wordmark plus four
      essential shortcuts (open, save, shortcuts reference, about),
      centered over an empty buffer, fading out on the first keystroke
      and back in if the buffer is emptied again — regardless of
      whether that got saved. Purely content-driven (no persisted
      "first launch" flag): also shows any time a file's contents get
      cleared, not just at initial launch. Verified live, including
      catching the fade-out and fade-back-in mid-animation. See
      [ADR 0042](adr/0042-empty-buffer-welcome-overlay.md).
- [x] **UI polish pass**: `kEaseFactor` (the single constant every
      eased value in `editor_viewport.cpp` shares — caret glide,
      scroll, diagnostic focus, the welcome overlay's fade) raised
      0.5 → 0.68 for snappier navigation, per direct feedback that
      Enter/Delete/general movement felt slow. Welcome overlay's
      shortcut list redesigned from flat centered lines to a
      two-column, dot-led layout (description left, key right). See
      [ADR 0043](adr/0043-faster-global-easing-and-welcome-overlay-layout.md).
- [x] **Panel isolation, full drag bar, unsaved-quit confirmation**:
      the five floating-panel popups (Find, File browser, Command
      line, Help, About) now fully isolate the document underneath —
      no typing, clicking, scrolling, or hover-popups through while
      one's open; closing is Escape-only (an early click-outside-
      closes design was rejected — double-clicks and clicks inside a
      panel bubbled the same way and closed it too, so it just blocks
      instead). Found and fixed two real, previously-latent bugs along
      the way: drag events on a panel's handle were never actually
      consumed, leaking through to the document; and releasing a drag
      stranded keyboard focus off the panel, so Escape could silently
      fail right after dragging. Help/About panels are now draggable
      from their whole header bar (no special hover cursor), not just
      the badge. Quitting with unsaved changes now asks for
      confirmation, styled to match the app's own dark theme instead
      of the native OS dialog look. See
      [ADR 0044](adr/0044-panel-isolation-drag-bar-unsaved-quit-confirm.md).
- [x] **v0.2.0-alpha**: the batch above was real new functionality, not
      just fixes — worth a real version bump. Also finally fixed the
      two open packaging bugs from `docs/feedback/0001` (deferred there
      until "a batch of useful additions," which this is): the `.deb`
      and AppImage are now built inside pinned old container bases
      (`debian:bookworm`, `ubuntu:22.04`) instead of natively on this
      rolling-release dev machine, which was baking in an unrealistic
      Qt/glibc floor. Found and fixed two more real bugs along the way
      (a `dpkg -S`/usrmerge symlink mismatch that silently dropped Qt6
      from the `.deb`'s dependencies; a missing `linuxdeploy-plugin-qt`
      env var for FUSE-less containers) — both verified by actually
      installing/running the rebuilt packages, not just inspecting
      them. See
      [ADR 0045](adr/0045-v0.2.0-alpha-and-containerized-packaging.md).
- [x] **Vim mode — Phase 1**: native (not a plugin — the plugin ABI
      has no raw-keystroke/modal-state hook), built inline into
      `EditorViewport`. Insert/Normal/Visual modes; motions
      `h j k l 0 ^ $ gg G w b e`; operators `d y c` composable with any
      motion and counts (`3j`, `d2w`, `3dd`/`yy`/`cc` linewise); `x`,
      `p`/`P`, `u`/`Ctrl+R`; Visual-mode `d y c x` over the existing
      selection model; `:<digits>` ex-command line jump. Off by default
      (`vim_mode = false`), byte-identical to before for anyone who
      hasn't opted in. Found and fixed one real bug during live
      verification: charwise `p` at end-of-line landed the paste on the
      *next* line instead of appending to the current one. Explicitly
      deferred for a later phase: registers beyond the clipboard,
      macros, marks, text objects, dot-repeat, jumplist, Visual Block, Replace mode, `r`, `:s///`/`:g//`, search-motion
      (`/ ? n N`), `J`, indent, case ops, and any keybinding remapping
      (project-wide gap, not Vim-specific — see ADR 0026). See
      [ADR 0046](adr/0046-native-vim-mode-phase-1.md).
- [x] **Vim mode — block cursor**: Normal mode now renders a real
      vim-style block cursor filling the character cell (measured/
      styled the same run-aware way `drawLine`/`xForColumn` are, so
      bold/italic captures and multi-byte UTF-8 characters size
      correctly), with the covered character knocked out in the
      background color on top so it stays legible, and its blink
      capped well under full opacity so it never flashes as a solid
      block. Insert/Visual keep the original bar caret. Reuses the
      existing position-glide and blink-reset animation machinery
      unchanged — no new animation path needed. See
      [ADR 0047](adr/0047-vim-block-cursor.md).
- [x] **Syntax accent colors, caret inset, smaller default font,
      immediate status label**: `ASE_HL_TYPE`/`ASE_HL_STRING` now
      render in two real accent colors instead of one being italic —
      `#689d6a` (the color the user previously supplied for the docs
      site, ADR 0036) for types, a matched warm gold for strings;
      everything else stays monochrome, per "don't use too much."
      Both caret shapes (bar and block) trimmed 2px shorter top/bottom.
      Default `font_size` 12 → 11. The status bar's mode label now
      shows immediately on launch instead of only after the first
      keystroke. A fourth request from the same feedback — a typing
      animation — was deliberately deferred pending a design decision
      rather than guessed at. See
      [ADR 0048](adr/0048-syntax-accent-colors-caret-inset-status-init.md).
- [x] **Typing pop-in animation**: the deferred fourth item from the
      same feedback. Each newly typed character (plain typing, Tab —
      not paste, not multi-line insertions, not Enter's own newline)
      fades and scales in from 85% to full size/opacity over ~120ms,
      gated on `animations = true` like every other animation in this
      editor. Hooks into `insertText`/`insertTextAt` (the one funnel
      every typing-shaped insertion already goes through) rather than
      adding a new call site. See
      [ADR 0049](adr/0049-typing-pop-in-animation.md).
- [x] **Runtime font-size zoom; Vim mode on by default**: `Ctrl+=`/
      `Ctrl+-`/`Ctrl+0` adjust font size live, in-session, independent
      of `config.ase` (survives an unrelated config hot-reload; only
      `Ctrl+0` or a restart returns to the configured value). Separately,
      Vim mode is now on by default (`vim_mode = true` in
      `ase_config_create_default()` and the shipped template — this
      reverses ADR 0046's original "opt-in" reasoning) and a session now
      starts in Normal mode, not Insert, matching real vim. See
      [ADR 0050](adr/0050-runtime-font-zoom-and-vim-mode-default-on.md).
- [x] **Global animation consistency; three Vim-mode polish fixes**:
      fixed a real bug where the typing pop-in (ADR 0049) never actually
      rendered — `snapAnimationToTarget()` was wiping its own animation
      entry in the same keystroke that created it. Removed the last
      special-cased instant-snaps from Vim mode (every motion, mode-entry
      command, and mutation) and from plain typing itself, so the whole
      app now glides consistently with no exceptions. Also: Visual mode
      now shows the same block cursor as Normal (was bar-only); the
      block cursor's opacity cap (ADR 0047) is gone, back to full range;
      and `:` opens the command line directly in Vim Normal/Visual mode,
      additive to the existing `Ctrl+;`. See
      [ADR 0051](adr/0051-global-animation-consistency-and-vim-polish.md).
- [x] **Split `EditorViewport` across translation units**: the one file
      had reached 3608 lines — 51% of the whole GUI layer — holding
      eight distinct concerns. Now nine `.cpp` files implementing the
      same class (config, render, input, edit, vim, find, commands, lsp,
      plus a small core), largest ~880 lines. `editor_viewport.h` is
      untouched: same class, same API, no `friend`s, nothing made public.
      Organisational, not architectural — coupling is unchanged, but
      extracting a real subsystem later now starts from a file that
      already holds exactly that subsystem. See
      [ADR 0052](adr/0052-editor-viewport-split-into-translation-units.md).
- [x] **The render hot path, and one motion language**: `capturesForLine()`
      scanned every highlight span for every visible line on every frame
      — measured **22% idle CPU** on an 8402-line file with the caret
      blinking. Flattened to a per-byte capture array built once per
      reparse, taking idle back to **0.0%**. Separately, six files had
      each independently picked their own animation duration; they now
      share `gui/src/motion.h`, which names the tiers and states the
      rule that new chrome picks one rather than inventing a seventh. See
      [ADR 0053](adr/0053-render-hot-path-and-one-motion-language.md).
- [x] **Multiple buffers, and the plugin host wired in**: a viewport per
      buffer behind a minimal custom-painted tab strip (no frames, no
      separators — state carried by opacity), `Ctrl+Tab`/`Ctrl+W`/`Ctrl+N`.
      The plugin host now actually loads `<config dir>/plugins/` and `:name`
      runs a registered command; it had existed as a tested core library
      with nothing calling it. See
      [ADR 0054](adr/0054-multiple-buffers-and-plugin-host-wiring.md).
- [x] **The dot means unsaved; new-file and panel fixes**: the tab dot
      moved from "this one is active" (which opacity already said) to
      "this one has unsaved changes" (which nothing said). `Ctrl+N`.
      Open/Save-As now resolve a typed path in *both* modes, confirm
      before overwriting, and show which directory you are in. About
      panel re-laid out as logo column + left-aligned text; the shortcut
      reference became data rather than a hand-written HTML blob, and
      caught up with everything that had shipped since it was written.
      See [ADR 0055](adr/0055-dirty-dot-new-file-panel-fixes.md).
- [x] **Linewise Visual, tab motion, searchable shortcuts**: `Shift+V`
      and `o` close two real gaps against vim. The tab strip reserves
      close-mark width on every tab so switching reflows nothing, and
      interpolates every layout change through the shared motion
      language. The shortcut reference became collapsible sections with
      a search field. See
      [ADR 0056](adr/0056-linewise-visual-tab-motion-searchable-shortcuts.md).
- [x] **An always-present tab strip, and a rewritten welcome screen**:
      the strip no longer appears and disappears under you; closing
      reverses opening; `Shift`+wheel pans it past the window edge. The
      welcome screen leads with what the editor *is*, then the shortcuts
      that matter — including the one that stops new users dead (Vim
      mode is on by default, so typing does nothing until `i`). See
      [ADR 0057](adr/0057-always-on-tab-strip-and-welcome-rework.md).
- [x] **Tab identity, the session greeting, and panel carets**: tabs were
      matched across layouts by *display name*, and every `+` buffer is
      called `untitled` — so new tabs animated from the wrong place and
      closing skipped its animation entirely. Keyed on the viewport
      pointer now. The welcome screen is armed only for a startup buffer
      opened with no file, never for a later `Ctrl+N`. And every panel
      text field draws the editor's own gliding, breathing caret instead
      of Qt's hard blink, closing the gap ADR 0028 left open. See
      [ADR 0058](adr/0058-session-scoped-greeting-and-panel-carets.md).
- [x] **Derived dirty state, vim paste position, four more motions**: from
      external testing. "Is this file modified?" is now a comparison
      against the undo stack's state id rather than a flag set by
      whoever touched the buffer last — type a space, undo it, and the
      file is correctly clean again, where before it stayed marked
      modified until saved. Charwise `p`/`P` leave the cursor on the
      last pasted character, as real vim does (linewise was already
      correct and was left alone). New: `{`/`}` paragraph motions,
      composable with operators, and `Ctrl+U`/`Ctrl+D` half-screen
      motions that move cursor and viewport together — the latter
      taking over `Ctrl+D` in Normal/Visual only, the single deliberate
      exception to ADR 0046's mode-independent Ctrl chain. See
      [ADR 0059](adr/0059-derived-dirty-state-paste-cursor-and-more-vim-motions.md).
- [x] **Linewise operations at the end of the buffer**: `dd` on the last
      line did nothing at all — the range was built as "through the start
      of the next line", and at the end of the buffer there is no next
      line, so it came out zero-length. Same asymmetry emptied rather
      than removed a non-empty last line, made `yy` there yank a
      fragment, and left a blank line behind on a linewise Visual delete.
      Linewise ranges that end at the buffer's end now take the
      *preceding* newline, in one shared helper. See
      [ADR 0060](adr/0060-linewise-operations-at-the-end-of-the-buffer.md).
- [x] **The unnamed register**: Vim mode had been using the system
      clipboard as its only register, which got both halves wrong —
      `d`/`x`/`c` filled nothing, so `dd` then `p` (the way you move a
      line) did nothing, and the obvious fix would have made every `x`
      destroy whatever you last copied from another application. There
      is now a real unnamed register, shared across buffers, filled by
      `y d c x` and read by `p`/`P`; the system clipboard is a separate
      destination on `Ctrl+C`/`Ctrl+V`, which yank still mirrors into
      (delete does not). See [ADR 0061](adr/0061-the-unnamed-register.md).
- [x] **The status bar message line**: the editor had no way to say
      anything at all — an unconfigured language server, a mistyped `:`
      command and a *failed write* were all silence, which reads as
      broken rather than as not-set-up. Everything now goes through one
      `notify()` funnel onto a message area sharing the status bar with
      the mode label: info dim and brief, warnings full strength and
      longer, errors in the existing diagnostic colour, staying until you
      move the cursor. See
      [ADR 0062](adr/0062-the-status-bar-message-line.md). Next on the
      same thread: a `:messages` history, and a persistent status-bar
      segment for language-server *state* (which is what the packaged
      LSP complaint actually needs).
- [x] **Language-server state in the status bar**: closes the external
      report that LSP "doesn't work in the packages". It wasn't a
      packaging bug — `lsp_command` ships commented out, and nothing
      anywhere distinguished "not configured" from "binary missing" from
      "working". A segment beside the cursor position now names the
      running server, says `no lsp` when there is none, and turns the
      diagnostic colour when one was configured but failed or died
      (noticed within a second, on the existing config-reload timer).
      See [ADR 0063](adr/0063-language-server-state-in-the-status-bar.md).
- [x] **The GUI in CI, and a test suite that runs in Release**: CI had
      built `-DASE_BUILD_GUI=OFF` on every job since Phase 1, which is
      why a Qt 6.6-only call could sit on `main` with both packages
      unbuildable until a human cut a release. There is now a job
      building the GUI on `ubuntu-22.04` (Qt 6.2 — the AppImage's own
      base) and launching it headless. It immediately found something
      worse: every test used `assert()`, which `NDEBUG` compiles out of
      Release builds, so **263 checks evaluated to nothing** and the 122
      that wrapped calls with side effects took the calls with them —
      three suites segfaulted, the rest passed while testing nothing.
      All of them are now an always-evaluated `CHECK()`. See
      [ADR 0064](adr/0064-gui-in-ci-and-a-test-suite-that-runs-in-release.md).
- [x] **Quick open (`Ctrl+P`)**: multiple buffers landed in ADR 0054,
      but the only way to *get* one was the directory-at-a-time browser.
      `Ctrl+P` walks the project (the enclosing git checkout, or the
      current folder), fuzzy-matches as you type, and ranks by
      consecutive runs, word boundaries, basename hits and brevity —
      `edvim` finds `editor_viewport_vim.cpp` first. Rides in the
      existing panel as a third mode rather than duplicating it. See
      [ADR 0065](adr/0065-quick-open.md).
- [x] **Project-wide search (`Ctrl+Shift+F`)**: find searched only the
      buffer in front of you; there was no way to answer "where else is
      this called?" without leaving the editor. The query rides in the
      find bar as a third mode, the results in the output panel — docked
      and non-modal, because reading a hit, looking at the code and
      coming back is the actual workflow and a floating panel blocks the
      document. Same file list as quick open, so the two can never
      disagree about what is in the project. Enter or a single click on
      a row opens that file at that line. See
      [ADR 0066](adr/0066-project-wide-search.md).
- [x] **Go to definition (`gd` / `F12`)**: the request had existed in
      `core/` since ADR 0011 with nothing in the GUI calling it. Both
      bindings work, in-file and across files, and every way it can fail
      now says why instead of the key appearing dead. The LSP language
      gate grew to C++ on the way — it had been `.c`/`.h` only, so this
      could not work on the editor's own sources. Named gap: there is no
      way to jump *back* yet (vim's jumplist is on `Ctrl+O`, which is
      Open here — the collision ADR 0046 flagged). See
      [ADR 0067](adr/0067-go-to-definition.md).
- [x] **Freed `Ctrl+O` and `Ctrl+I`** for Vim's jumplist by moving the
      two panels that held them: the shortcut reference to `F1` (where it
      belonged anyway), About to `Alt+I`, Open to `Alt+O`. A panel opened
      a few times a session can afford an unusual binding; a navigation
      key used constantly cannot. See
      [ADR 0068](adr/0068-freeing-ctrl-o-and-ctrl-i.md).
- [x] **Find-in-line (`f F t T ; ,`) and `Ctrl+J`/`Ctrl+K` in every
      list**: two requests that are the same request underneath — not
      leaving the home row. The find motions are line-confined like
      vim's, take counts that fail as a unit, and are inclusive under an
      operator (`df,` takes the comma). List navigation went into one
      shared helper rather than four copies of the same check. See
      [ADR 0069](adr/0069-find-in-line-and-list-navigation.md).
- [x] **The jumplist (`Ctrl+O` / `Ctrl+I`)**: go-to-definition made this
      impossible to ignore — you could follow a name into a file you did
      not choose with no way back. Same list-plus-index shape as the undo
      stack, owned by the window because jumps cross buffers, recording
      only movements you could not have made with `h j k l`. Entries
      carry a path as well as a buffer address, so a closed file is
      reopened rather than skipped. See
      [ADR 0070](adr/0070-the-jumplist.md).
- [x] **v0.3.0-alpha**: everything since v0.2.0-alpha — Vim mode and its
      polish, the block cursor, syntax accent colors, runtime font zoom,
      the typing pop-in, the `EditorViewport` split, the render hot-path
      fix, one motion language, multiple buffers, the plugin host wired
      in, and the tab strip — is a substantially different editor from
      the one that tag points at.
- [x] **v0.4.0-beta**: the first release not called alpha. Everything
      since v0.3.0-alpha — find-references and document symbols, a
      keyboard with a law rather than a free-slot policy, regions and
      the `Ctrl+W` vocabulary, built-in palettes, keybindings as data,
      session restore, git gutter marks, an atomic save, crash recovery
      that now covers the unnamed buffer too, and a documentation site
      whose reference pages are generated from the editor's own tables.
      Beta because the daily-driver gaps are closed and the data-loss
      story is, not because nothing is left: see the follow-ups below,
      and note that Linux is the supported platform.

## Beyond v1 — what would make this editor unique

Deliberately grouped by *why* rather than by area. The ordering inside
each tier is rough; the tiers themselves are not — Tier 1 is what stops
the editor being embarrassing on a real codebase, Tier 2 is what makes it
genuinely usable all day, Tier 3 is what makes it worth choosing over
something else.

### Tier 1 — make the existing promises true

- ~~**Incremental Tree-sitter reparse.**~~ Done, and the estimate here was
  an order of magnitude low: the real cost was **~177ms per keystroke** on
  a 10,800-line file (107ms reparse, 64ms whole-file query). Now ~7ms —
  the parse is incremental, the query runs over a window around what is
  on screen, and the line scan uses `memchr`. See
  [ADR 0072](adr/0072-incremental-highlighting-and-a-measured-hot-path.md),
  which also lists what is next in measured order (caret-rect repaints,
  incremental line starts).
- ~~**Multiple buffers / files per window.**~~ Done — a viewport per
  buffer behind a minimal dot-and-name bar, `Ctrl+Tab`/`Ctrl+W` to
  switch and close. See
  [ADR 0054](adr/0054-multiple-buffers-and-plugin-host-wiring.md).
- ~~**Wire the plugin host into the GUI.**~~ Done — `<config dir>/plugins/`
  loads at startup and `:name` runs any registered command (ADR 0054).
  The ABI is still the narrow one; widening it is the next plugin step,
  see [EXTENSIBILITY.md](EXTENSIBILITY.md).
- ~~**One language server per project, not per visited file.**~~ Done —
  a registry keyed by language and project root, so four C buffers share
  one `clangd` instead of four. Measured at the time: 1601 MB to 919 MB
  ([ADR 0096](adr/0096-one-server-per-language-and-project.md)).
- ~~**Show unsaved state in the buffer bar.**~~ Done — the dot now marks
  unsaved changes rather than the active buffer, and stays readable on a
  dimmed entry ([ADR 0055](adr/0055-dirty-dot-new-file-panel-fixes.md)).
- ~~**Buffer bar overflow.**~~ Done — `Shift`+wheel pans the strip
  ([ADR 0057](adr/0057-always-on-tab-strip-and-welcome-rework.md)). Tabs
  still don't elide long names, which is the remaining half.
- ~~**Large-file sanity pass.**~~ Done, and the ceiling was real: the
  Tree-sitter parse is linear in file size and lands on the UI thread
  twice — 1.07s to open 4.5MB of C, and 20ms per keystroke after that.
  The re-highlight now waits for a pause in typing, and past
  `syntax_max_kb` a file opens with no colours rather than freezing
  first, saying so in the status bar
  ([ADR 0107](adr/0107-the-large-file-wall.md)).

### Tier 2 — the table stakes of a daily driver

- ~~**Keybindings as data**~~ Done — `key.<chord> = <command>` in
  `config.ase`, with built-ins, window actions and plugin commands in one
  registry, so binding a key to a plugin needs no new API. Vim's own keys
  are moved a different way, by remapping rather than by binding
  ([ADR 0113](adr/0113-keybindings-as-data.md),
  [ADR 0134](adr/0134-vim-keys-you-can-move.md)).
- ~~**Project-wide search**~~ Done for the read-only half — search, and
  jump to a hit ([ADR 0066](adr/0066-project-wide-search.md)). **Replace
  across files** is deliberately still open: editing every file in a
  project from one keystroke wants its own design pass (a preview, an
  undo story spanning buffers), not a ride-along on a read-only
  feature.
- **The rest of LSP's useful half**: ~~go-to-definition~~
  ([ADR 0067](adr/0067-go-to-definition.md)), ~~a jumplist~~
  ([ADR 0070](adr/0070-the-jumplist.md)), ~~find-references~~ and
  ~~document symbols~~
  ([ADR 0116](adr/0116-find-references-and-document-symbols.md)) are in.
  ~~rename~~ is in too ([ADR 0132](adr/0132-renaming-a-symbol.md)): the
  WorkspaceEdit it returns becomes the same edit set a project replace
  produces, previewed before anything happens and applied into buffers.
- ~~**Git gutter marks**~~ Done — added, changed and deleted per line,
  from `git diff -U0`'s hunk headers, hidden while an unsaved edit has
  moved the lines around ([ADR 0112](adr/0112-git-gutter-marks.md)).
- ~~**Session restore**~~ Done — files, carets and scroll positions, only
  when started with no file argument
  ([ADR 0111](adr/0111-session-restore.md)).
- ~~**More languages.**~~ Done — C, C++, Python, JavaScript, CSS, HTML
  and Lua, each a `.scm` query plus a build entry, with the LSP language
  table widened to match
  ([ADR 0103](adr/0103-the-mainstream-languages.md)).

### Tier 3 — the bets that would make it distinctive

- **Smart compile.** Instead of a hand-written `build_command`, infer the
  right action: read `compile_commands.json` when clangd is configured
  (it already records the exact invocation per file), else walk up for a
  `Makefile`/`CMakeLists.txt`. **Show the inferred command for
  confirmation rather than silently running it** — this project's
  standing rule is that an unconfigured thing is reported, never guessed
  (ADR 0029), and a misdetected build target running silently would
  violate that badly.
- **A TUI frontend on the same core.** The headless-core split (ADR 0002)
  is already paid for and tested, and nothing about the buffer, undo,
  syntax, LSP or plugin layers is Qt-specific. A terminal frontend
  sharing the exact core is a genuinely unusual thing to be able to
  offer, and the architecture has been quietly holding the door open for
  it since Phase 1.
- **Motion as a designed identity.** The animation language is now one
  header (ADR 0053) rather than six scattered guesses. Leaning into that
  deliberately — every state change in the app having a considered,
  consistent transition — is a real differentiator, because almost no
  editor treats it as a design system instead of as decoration.
- **Scriptable/batch mode.** The core runs headless; `ase --script
  fix.lua file.c` reusing the same plugin commands as the GUI would make
  plugins testable without a display, and the editor usable in a pipeline.
- **Remote/SSH editing** and **collaborative editing** stay explicit
  non-goals below — listed here only to say they were considered and
  deliberately declined, not overlooked.

### Still open, in rough order of value

1. ~~**LSP rename.**~~ Done — `F2`, previewed like a project replace and
   applied into buffers, so nothing is written until you save
   ([ADR 0132](adr/0132-renaming-a-symbol.md)).
2. ~~**Theme files.**~~ Done — `<config dir>/themes/<name>.ase`, the
   same keys a config uses, inheriting the defaults for whatever it
   leaves out ([ADR 0133](adr/0133-a-theme-you-can-share-as-a-file.md)).
3. ~~**Vim sequences as data.**~~ Done, as a remap rather than a table:
   `vim.normal.<key> = <keys>` stands in for a key before the dispatcher
   sees it, so counts and pending operators compose with it
   ([ADR 0134](adr/0134-vim-keys-you-can-move.md)). The dispatcher
   itself is still a state machine, deliberately — `d2w` is not a table
   entry.
4. ~~**An unnamed buffer gets no crash snapshot.**~~ Done — a snapshot is
   filed under a key rather than a path, and untitled buffers are
   offered back at startup because nothing else will ever reopen them
   ([ADR 0127](adr/0127-the-buffer-with-nothing-to-fall-back-on.md)).
5. ~~**Snapshots are never pruned.**~~ Done — anything untouched for 30
   days goes at startup (ADR 0127).
6. ~~**A concurrent parse.**~~ Done — the parse runs on a worker thread
   past 256KB, so a large file is responsive *and* coloured, including
   while being typed into. The size cap stays, now bounding memory
   rather than lag
   ([ADR 0135](adr/0135-the-parse-moves-off-the-ui-thread.md)).
7. ~~**Replace across files.**~~ Done — `Alt+R`, previewed before it
   happens, applied into buffers rather than onto disk so nothing is
   written until you save
   ([ADR 0131](adr/0131-replacing-across-files.md)). **LSP rename** is
   still open, but now only needs a `WorkspaceEdit` turned into the edit
   set that machinery already takes.
8. **Per-capture syntax colours as data** — EXTENSIBILITY.md,
   recommendation 5.

## Explicit non-goals for v1

- No built-in terminal emulator (defer to external terminal or a later plugin).
- No remote/SSH editing.
- No collaborative/multiplayer editing.
- No plugin marketplace/registry infrastructure — plugins are install-by-file.

## Open decisions blocking later phases

None currently open — the license decision (the last one tracked
here) was resolved by
[ADR 0033](adr/0033-license-apache-2.0-and-first-alpha-release.md)
(Apache License 2.0), alongside the project's first alpha release,
`v0.1.0-alpha`.

## Follow-ups noted but not yet scheduled

Audited 2026-09-17: six entries here described things that had been done
— keybinding customization, the plugin directory loading, the GUI
triggering a plugin command, the LSP being wired in, `didChange`, and
panel infrastructure. They are removed. What is below was checked
against the code on that date.

- **No screen-reader text exposure** (`QAccessibleInterface`) for the
  custom-painted viewport — a real, open accessibility gap, not
  attempted because it is unverifiable without a live AT-SPI client in
  this environment (ADR 0012, decision 4). Needs dedicated follow-up
  with proper assistive-technology test tooling.
- **Three core suites segfault on Windows.** `ase_config_tests` inside
  `ase_config_create_default()`, `ase_theme_tests` while reading a static
  table it has just been handed a valid pointer into, and
  `ase_plugin_host_tests` immediately after registering a command. None
  reproduces on Linux or macOS, with or without sanitizers. The job runs
  in CI without gating so the signal is not lost
  ([ADR 0130](adr/0130-the-ci-nobody-was-reading.md)).
- **The LSP client and `:compile` are POSIX-only.**
  `ase_process_spawn()` returns NULL on Windows (`core/src/process.c`),
  so both features are present and do nothing there. Async child-process
  I/O on Windows needs overlapped I/O or a reader thread (ADR 0011,
  decision 6) — real work, not a quick add-on.
- Counts on `s`, `S`, `C` and `c`-with-a-motion are ignored. The other
  six insert commands read theirs
  ([ADR 0137](adr/0137-a-count-before-an-insert.md)); these four each
  have their own deletion semantics to settle first.

