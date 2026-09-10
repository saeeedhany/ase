# ADR 0030: LSP completion + hover (Phase 17)

## Status

Accepted

## Context

Phase 16 (ADR 0029) wired diagnostics end to end but left completion
and hover as core-only stubs — `ase_lsp_client_request_completion`
existed but nothing in the GUI ever called it, and hover didn't exist
in the client at all. This phase closes both, with two deliberate,
user-chosen departures from this project's usual "manual trigger"
philosophy (`:compile`'s Ctrl+B, the command line's Ctrl+;): completion
fires automatically while typing, and hover fires from the mouse
pausing over a symbol, rather than either needing a keybinding. Both
were an explicit choice — the alternative (manual triggers, e.g.
Ctrl+Space and Ctrl+K) was offered and declined.

## Decision

### Core: `textDocument/hover`

`ase_lsp_client_request_hover(client, uri, position, callback,
user_data)` — same request/response shape as completion/definition,
the raw JSON result handed back unparsed (a hover response's
`contents` varies by server: a plain string, a MarkupContent
`{kind, value}`, a single legacy MarkedString, or an array of them —
parsing that variety belongs at the call site, not duplicated as a
core-level union type). `fake_lsp_server.c` and `test_lsp_client.c`
got a matching canned-response/assertion pair, following Phase 16's
pattern exactly.

### No request-sequence tracking — a deliberate v1 simplification

Neither completion nor hover requests carry a sequence token to
discard stale, out-of-order responses. `AseLspResultCallback`'s
`user_data` is fixed at registration time (always `this`), so
disambiguating would need a small heap-allocated context per request —
and `ase_lsp_client_stop()` never fires a still-pending request's
callback (the pending-request table is simply freed, not drained), so
a request in flight when the client stops (e.g. `openFile()` switching
documents) would leak that context. Avoiding the allocation entirely
avoids the leak. In practice a request is local and fast (well under
typing speed), nothing is ever auto-applied without the user pressing
Enter/Tab, and a hover response never clobbers an already-good tooltip
(see `applyLspHover`'s doc comment) — so the worst case is a one-
keystroke-stale completion list that the very next response corrects,
not a correctness bug.

### Completion: gated automatic trigger, not "after every keystroke"

`requestCompletionIfAppropriate()` runs from the end of
`refreshCache()` — the same choke point `sendLspDidChange()` already
uses — but only actually sends a request when exactly one cursor with
no active selection sits right after an identifier character or a
member-access trigger (`.`, or the `>` of `->`; C has no `::`).
Anything else (a space, a newline, punctuation) dismisses whatever's
open instead of firing a request. Without this gate, "automatic"
would mean a wall of irrelevant global-symbol completions after every
space — every real editor's automatic mode gates the same way client-
side, LSP servers don't do it for you.

Accepting (`acceptCompletion()`, wired to Enter and Tab while the
popup is showing) replaces `[m_completionPrefixStart, cursor)` with
the selected item's `insertText` by giving `insertText()` a temporary
single-cursor selection over that range — it already knows how to
replace an active selection (docs/adr/0019), so this reuses that path
instead of duplicating replace-range logic. `m_completionPrefixStart`
is recomputed on every request by scanning backward from the cursor
while bytes are identifier characters (reusing the existing
`isWordChar` predicate) — the same byte-level "ASCII v1
simplification" this codebase already applies to LSP character offsets
(docs/adr/0029).

A real, fixed rough edge found during live verification: accepting
inserts an identifier, which itself typically still ends in a word
character, so the very insert that follows (`insertText()` calling
`refreshCache()` calling `requestCompletionIfAppropriate()` again)
would immediately reopen a fresh popup showing the item *just*
accepted — not what any editor actually does. Fixed with
`m_suppressNextCompletionTrigger`, a one-shot flag set right before
`acceptCompletion()`'s `insertText()` call and consumed by the very
next `requestCompletionIfAppropriate()`.

### Hover: pause-then-request, word-range aware dismissal

`EditorViewport::mouseMoveEvent` now also runs with no mouse button
held (`setMouseTracking(true)`, new in this phase — previously the
widget only got move events during an active drag). A qualifying move
calls `scheduleHoverRequest()`, which restarts a 500ms single-shot
timer (`m_hoverTimer`) unless the pointer is still within the buffer
range the *currently shown* tooltip covers — avoiding a flicker/re-
request loop from ordinary small hand jitter while reading a tooltip.
That range comes from the response's own `range` field when the
server sends one (clangd does), falling back to a client-side
identifier-run scan around the requested offset otherwise. The
tooltip also dismisses on: any key press, a click, a wheel scroll (the
screen point it was anchored to is now wrong), the mouse leaving the
widget, and the widget losing focus (`leaveEvent`/`focusOutEvent`,
both new overrides).

No markdown rendering — `extractHoverText()` pulls plain text out of
whichever `contents` shape the server sent (string / `{value}` object
/ array of either, joined with blank lines) and shows it as-is in a
word-wrapped label. clangd's plaintext hover (type, value, containing
scope, the declaration line) reads perfectly well unstyled and matches
this app's austere aesthetic; full markdown rendering was cut as
scope not worth it for v1.

### Two new widgets, deliberately not `FloatingPanel`

`CompletionPopup` and `HoverPanel` (`gui/src/completion_popup.{h,cpp}`,
`gui/src/hover_panel.{h,cpp}`) share `FloatingPanel`'s flat/translucent
visual language (same `panelBackgroundColor()`/`panelBorderColor()`
accessors) but don't derive from it and don't use its host-centered
anchor system or snapshot-based scale+fade animation
(docs/adr/0022, docs/adr/0024). Both track a moving point (the caret,
the mouse) and refresh content far more often than a glance-act-
dismiss chrome window like Find/Replace — replaying a scale-pop
animation on every keystroke would read as busy, not smooth, so each
only fades (faster, 90ms vs. FloatingPanel's 110ms) and only on the
hidden→visible edge; an already-open widget just updates content and
geometry instantly. Positioning is a plain `move()`/`setGeometry()`
clamped to stay inside the viewport, flipping above the anchor point
instead of below when there's no room.

`CompletionPopup` paints its own rows via `QPainter` rather than using
`QListWidget` — this codebase already hit a real class of "geometry
wrong until some later Qt-internal layout pass" bugs with
`QListWidget` in `FileBrowserPanel` (docs/adr/0024), and this widget
resizes on nearly every keystroke, far more often than that one ever
did. `HoverPanel` sizes its wrapped text the same way, using
`QFontMetrics::boundingRect(..., Qt::TextWordWrap, text)` directly
instead of reading a size back from `QLabel` after the fact — the same
"measure explicitly, don't trust a widget's own lazy layout pass"
reasoning, echoing `xForColumn`'s approach to caret positioning
(docs/adr/0013).

Both are wired into `main.cpp` exactly like every other panel:
constructed as children of `viewport`, handed back to it via a setter
(`setCompletionPopup`/`setHoverPanel`), theme-refreshed on config
hot-reload alongside the rest.

## Consequences

All 9 ctest suites pass, including the new
`ase_lsp_client_tests` hover round-trip. Verified live against real
`clangd`: typing `pri` inside `main()` produced a live-updating,
correctly-positioned completion popup with real macro completions
from `<stdio.h>`'s preamble (clangd itself prefixes macro-kind labels
with `•` — confirmed as the server's own convention, not a parsing
bug, by inspecting the rendered rows at high zoom); navigating with
arrow keys and accepting with Enter correctly replaced the typed
prefix (`pri` → `PRId64`) with no leftover duplicate text and no
immediate popup reopen; hovering over `value` after a ~500ms pause
showed clangd's real hover payload (`variable value`, `Type: int`,
`Value = 42 (0x2a)`, the declaration line) in a correctly word-wrapped
panel; moving the mouse away dismissed it.

Not attempted: markdown rendering of hover content (plain text only —
see above); completion item `kind`-based icons/badges (label + detail
only, no per-kind glyph); incremental/fuzzy client-side filtering of
an already-fetched list as the user keeps typing past what the last
response covers (every keystroke sends a fresh request instead,
acceptable at local-clangd latency); go-to-definition
(`ase_lsp_client_request_definition`) still has no GUI wiring — it
remains, like completion was before this phase, implemented at the
core layer only.
