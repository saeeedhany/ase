# Architecture Decision Records

Every architecturally significant decision made on this project, in the
order it was made — see [ADR 0001](0001-record-architecture-decisions.md)
for why this project keeps these at all. This is the project's history,
told through the reasoning behind each choice rather than a changelog of
what changed.

| # | Decision |
|---|----------|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions with ADRs |
| [0002](0002-headless-core-separation.md) | Core engine builds and runs headless |
| [0003](0003-license-decision-pending.md) | License is deliberately undecided |
| [0004](0004-core-in-plain-c.md) | Core engine is plain C, not C++ |
| [0005](0005-buffer-engine-piece-table.md) | Buffer engine is a piece table, not a rope |
| [0006](0006-gui-shell-v1-shortcuts.md) | Phase 2 GUI shell — deliberate v1 shortcuts |
| [0007](0007-syntax-highlighting-tree-sitter.md) | Syntax highlighting via Tree-sitter, monochrome by design |
| [0008](0008-config-theme-format.md) | Config/theme format is a minimal hand-rolled key=value format, not TOML |
| [0009](0009-plugin-abi-and-lua-host.md) | Plugin ABI + Lua host, unified under one command registry |
| [0010](0010-hand-rolled-json.md) | Hand-rolled JSON, not a vendored library |
| [0011](0011-lsp-client.md) | LSP client — process isolation, sync handshake, async everything else |
| [0012](0012-polish-phase-scope.md) | Phase 7 polish — scope and an accessibility fix |
| [0013](0013-caret-drift-fix.md) | Fix caret drift — measure rendered text, don't assume a fixed pitch |
| [0014](0014-viewport-geometry.md) | Viewport geometry — horizontal scroll, view-follow, line numbers |
| [0015](0015-smooth-motion.md) | Smooth motion — caret glide and smooth scroll |
| [0016](0016-caret-visibility-fix.md) | Fix invisible caret; blink resets on activity, not on a fixed cycle |
| [0017](0017-viewport-review-pass.md) | Viewport review pass — four fixes from live use |
| [0018](0018-undo-redo.md) | Undo/redo — diff-based groups on top of the buffer's own primitives |
| [0019](0019-selection-model.md) | Selection model — a per-cursor anchor, not a separate range type |
| [0020](0020-clipboard.md) | Clipboard — cut/copy/paste on top of the selection model |
| [0021](0021-find-replace.md) | Find & replace — a keyboard-only bar, matches as selections |
| [0022](0022-floating-panel-design-system.md) | A unified floating-panel design system for editor chrome |
| [0023](0023-editor-chrome.md) | Editor chrome — status bar, dirty tracking, and a custom Open/Save-As panel |
| [0024](0024-panel-polish-and-scroll-margin.md) | Panel animation rework, file-browser search/highlight, scroll margin, status bar theme |
| [0025](0025-command-system.md) | Command line + `:compile` + output panel |
| [0026](0026-keybinding-scheme-help-about.md) | Keybinding scheme fix, Help/About panels, find bar repositioned |
| [0027](0027-visual-polish-pass.md) | Logo, title-color bug, thin scrollbars, faster animation, output divider |
| [0028](0028-click-precision-select-all-deletion-glide.md) | Click precision fix, deletion glide, select-all, About centering, synced input blink |
| [0029](0029-lsp-diagnostics-wiring.md) | LSP diagnostics wiring (Phase 16) |
| [0030](0030-lsp-completion-hover.md) | LSP completion + hover (Phase 17) |
| [0031](0031-draggable-panels-smooth-scroll-tracking-glide.md) | Draggable panels, smooth scroll, tracking-popup glide, Tab |
| [0032](0032-lsp-relative-path-uri-fix.md) | Fix LSP URI construction for relative paths |
| [0033](0033-license-apache-2.0-and-first-alpha-release.md) | License chosen (Apache 2.0) — first alpha release |
| [0034](0034-linux-packaging.md) | Linux packaging (AppImage, Arch PKGBUILD, .deb) |
| [0035](0035-mkdocs-documentation-site.md) | MkDocs documentation site |
| [0036](0036-docs-site-theme-and-plugins-page.md) | Docs site branding (logo, editor colors) and a Plugins page |
| [0037](0037-feedback-triage-log.md) | A feedback triage log, alongside ADRs |
| [0038](0038-feedback-triage-criteria-and-deferred-status.md) | Feedback triage criteria, and a "Deferred" status |
| [0039](0039-mouse-hit-test-drift-fix.md) | Fix mouse click/hover drift on styled lines |
| [0040](0040-italic-glyph-clipping-fix.md) | Fix italic glyphs clipped at the top |
| [0041](0041-diagnostic-underline-and-dot-focus.md) | Diagnostic underline redesign — thin line, dim/focus states |
| [0042](0042-empty-buffer-welcome-overlay.md) | Empty-buffer welcome overlay |
| [0043](0043-faster-global-easing-and-welcome-overlay-layout.md) | Faster global easing; welcome overlay two-column layout |
| [0044](0044-panel-isolation-drag-bar-unsaved-quit-confirm.md) | Panel isolation, full drag bar, unsaved-quit confirmation |
| [0045](0045-v0.2.0-alpha-and-containerized-packaging.md) | v0.2.0-alpha, and packaging finally built on pinned old bases |
| [0046](0046-native-vim-mode-phase-1.md) | Native Vim mode, Phase 1 |
| [0047](0047-vim-block-cursor.md) | A real block cursor for Vim Normal mode |
| [0048](0048-syntax-accent-colors-caret-inset-status-init.md) | Syntax accent colors, caret inset, smaller default font, immediate status label |
| [0049](0049-typing-pop-in-animation.md) | A typing pop-in animation |
| [0050](0050-runtime-font-zoom-and-vim-mode-default-on.md) | Runtime font-size zoom, and Vim mode on by default |
| [0051](0051-global-animation-consistency-and-vim-polish.md) | Global animation consistency, and three Vim-mode polish fixes |
| [0052](0052-editor-viewport-split-into-translation-units.md) | Splitting EditorViewport across several translation units |
| [0053](0053-render-hot-path-and-one-motion-language.md) | Fixing the render hot path, and one motion language |
| [0054](0054-multiple-buffers-and-plugin-host-wiring.md) | Multiple buffers, and finally wiring the plugin host in |
| [0055](0055-dirty-dot-new-file-panel-fixes.md) | The dot means unsaved, plus new-file and panel fixes |
| [0056](0056-linewise-visual-tab-motion-searchable-shortcuts.md) | Linewise Visual, tab motion, and a searchable shortcut reference |
| [0057](0057-always-on-tab-strip-and-welcome-rework.md) | An always-present tab strip, and a welcome screen worth reading |
| [0058](0058-session-scoped-greeting-and-panel-carets.md) | The greeting belongs to the session; panel carets join the app |
| [0059](0059-derived-dirty-state-paste-cursor-and-more-vim-motions.md) | Dirtiness derived from undo state; vim paste cursor; `{` `}` `Ctrl+U` `Ctrl+D` |
| [0060](0060-linewise-operations-at-the-end-of-the-buffer.md) | Linewise operations at the end of the buffer (`dd` on the last line) |
| [0061](0061-the-unnamed-register.md) | The unnamed register — `dd`+`p` works, and `x` stops eating your clipboard |
| [0062](0062-the-status-bar-message-line.md) | The status bar message line — the editor can finally say things |
| [0063](0063-language-server-state-in-the-status-bar.md) | Language-server state in the status bar |
| [0064](0064-gui-in-ci-and-a-test-suite-that-runs-in-release.md) | The GUI in CI, and a test suite that actually runs in Release |
| [0065](0065-quick-open.md) | Quick open (`Ctrl+P`) — fuzzy file finding across the project |
| [0066](0066-project-wide-search.md) | Project-wide search (`Ctrl+Shift+F`) |
| [0067](0067-go-to-definition.md) | Go to definition (`gd` / `F12`) |
| [0068](0068-freeing-ctrl-o-and-ctrl-i.md) | Freeing `Ctrl+O`/`Ctrl+I`: help to `F1`, About/Open to `Alt` |
| [0069](0069-find-in-line-and-list-navigation.md) | Vim's `f`/`t` find-in-line, and `Ctrl+J`/`Ctrl+K` in every list |
| [0070](0070-the-jumplist.md) | The jumplist (`Ctrl+O` / `Ctrl+I`) |
| [0071](0071-block-cursor-knockout-follows-its-block.md) | The block cursor's knockout follows its block |
| [0072](0072-incremental-highlighting-and-a-measured-hot-path.md) | Incremental highlighting, and a measured hot path |
| [0073](0073-the-command-line-moves-to-the-status-bar.md) | The command line moves to the status bar |
| [0074](0074-search-with-slash-and-n.md) | `/` search, and a needle that outlives its panel |
| [0075](0075-repeating-the-last-change.md) | Repeating the last change (`.`) |
| [0076](0076-replace-a-character.md) | Replacing a character (`r`) |
| [0077](0077-replace-mode.md) | Replace mode (`R`) |
| [0078](0078-visual-mode-replace.md) | Visual-mode `r`, and a selection that is off by one |
| [0079](0079-the-visual-selection-includes-the-cursor.md) | The visual selection includes the character under the cursor |
| [0080](0080-one-undo-step-per-insert-session.md) | One undo step per insert session |
| [0081](0081-cw-changes-to-the-end-of-the-word.md) | `cw` changes to the end of the word |
| [0082](0082-a-bare-modifier-must-not-clear-a-count.md) | A bare modifier must not clear a pending count |
| [0083](0083-the-shorthand-operators.md) | The shorthand operators (`s`, `S`, `C`, `D`, `X`) |
| [0084](0084-bracket-matching-and-word-search.md) | `%`, `*`/`#`, and where a search lands |
| [0085](0085-a-second-grammar-for-cpp.md) | A second grammar for C++ |
| [0086](0086-one-language-gate-and-per-language-keys.md) | One language gate, and per-language config keys |
| [0087](0087-project-config-says-what-files-mean.md) | A project file says what files mean, never what to run |
| [0088](0088-config-documents-itself.md) | Config keys document themselves |
| [0089](0089-closing-a-buffer.md) | `:q` closes a buffer, and closing stopped sleeping |
| [0090](0090-scrolling-runs-at-frame-rate.md) | Scrolling runs at frame rate |
| [0091](0091-the-clock-follows-the-display.md) | The animation clock follows the display |
| [0092](0092-a-discarded-server-is-not-waited-for.md) | A discarded server is not waited for |
| [0093](0093-the-handshake-is-not-awaited.md) | The initialize handshake is not awaited |
| [0094](0094-what-startup-actually-costs.md) | What startup actually costs |
| [0095](0095-the-performance-baseline.md) | The performance baseline |
| [0096](0096-one-server-per-language-and-project.md) | One server per language and project, not per buffer |
| [0097](0097-marks-and-macros.md) | Marks and macros |
| [0098](0098-global-marks.md) | Global marks reuse the jumplist’s entry |
| [0099](0099-vim-conformance-fixes.md) | Four vim rules the editor was missing |
| [0100](0100-text-objects.md) | Text objects |
| [0101](0101-join-lines.md) | `J` joins lines |
| [0102](0102-substitute.md) | `:s`, and the regex dialect it speaks |
| [0103](0103-the-mainstream-languages.md) | The mainstream languages |
| [0104](0104-the-vim-conformance-suite.md) | The vim conformance suite |
| [0105](0105-named-registers.md) | Named registers, and a test that was lying |
| [0106](0106-words-case-indent-numbers.md) | WORDs, case, indent and numbers |
| [0107](0107-the-large-file-wall.md) | The large-file wall |
| [0108](0108-the-half-typed-command.md) | The half-typed command |
| [0109](0109-saving-without-losing-the-file.md) | Saving without losing the file |
