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
      `QFileDialog` — overriding what
      `~/.claude/plans/noble-herding-quokka.md`'s Phase 14 section
      originally sketched; that plan needs a matching update before
      Phase 14 starts. `main.cpp` reverted to its pre-Phase-13
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
      macros, marks, text objects, dot-repeat, jumplist, Visual
      Block/Line, Replace mode, `r`, `:s///`/`:g//`, search-motion
      (`/ ? n N`), `J`, indent, case ops, and any keybinding remapping
      (project-wide gap, not Vim-specific — see ADR 0026). See
      [ADR 0046](adr/0046-native-vim-mode-phase-1.md).

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
