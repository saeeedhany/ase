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
