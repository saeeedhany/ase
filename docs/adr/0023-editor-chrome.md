# ADR 0023: Editor chrome — status bar, dirty tracking, and a custom Open/Save-As panel

## Status

Accepted

## Context

Phase 14 of the "complete normal editor" plan: status bar, dirty
tracking (explicitly deferred since ADR 0006), and Open/Save-As. The
original sketch for this phase used native
`QFileDialog::getOpenFileName`/`getSaveFileName` —
superseded mid-flight by [ADR 0022](0022-floating-panel-design-system.md),
written right after Phase 13.5's find-bar restyle, when the user
decided future dialogs should join the same custom floating-panel
system rather than stay native. That decision is executed here: a real
`FileBrowserPanel`, not a one-line `QFileDialog` call.

## Decision

### Dirty tracking

`bool m_dirty` on `EditorViewport`, set `true` at the end of every
batch mutation (`insertText`, `deleteBackward`, `deleteForward`,
`cutSelection`, `replaceAllMatches`, `applyUndoResult` — the last one
per the plan's "kept simple" call: any undo/redo marks dirty too,
rather than tracking the exact saved-stack position). Cleared by a
*successful* `save()`. `pasteClipboard`/`replaceCurrentMatch` route
through `insertText` already, so they're covered without their own
call site.

### Status bar and title: one signal, wired at one call site

`EditorViewport` gets its first-ever `Q_OBJECT`/signal:
`void statusChanged(int line, int column, bool dirty)`, 1-based for
display. Rather than annotating every one of the many call sites that
move the cursor or edit the buffer, it's emitted from inside
`ensureCursorVisible()` itself — every such call site already calls
that (scrolling the cursor into view is meaningless without knowing
where it is), so this is the one true choke point, cheaper than the
plan's original per-call-site sketch. `main.cpp` connects it to a
`QStatusBar` label (`"Ln %1, Col %2"` + `" *"` when dirty) and the
window title (`"<path|untitled> [modified] — Absolute Simple Editor"`).

### Open/Save-As: `FileBrowserPanel`, a second `FloatingPanel`

New `gui/src/file_browser_panel.{h,cpp}`, same family as `FindBar`:
one `LetterBadge` — reused for both modes via the new
`LetterBadge::setLetter` rather than two separate badges (`FindBar`'s
approach doesn't generalize as well here since only one mode is ever
active, unlike Find/Replace's two simultaneous fields) — a path field
that doubles as the current-directory display (typing a name into it
after navigating "just works" relative to wherever you are), and a
`QListWidget` directory listing.

- **Listing**: `QDir`, dirs-first then alphabetical, dotfiles excluded
  with no toggle (v1 simplification, easy to add later, not a half-
  built feature — there's no partial "sometimes shows dotfiles" state).
- **Navigation**: activating (Enter or double-click) a directory entry
  or `..` navigates; activating a file **opens it immediately in Open
  mode**, but **only fills the path field in Save-As mode** — a stray
  double-click on an existing file shouldn't silently queue an
  overwrite. The path field's own Enter always commits (open or save)
  once it resolves to a non-directory path.
- **`EditorViewport::openFile`**: destroys the current buffer/syntax/
  undo stack and loads fresh, resetting every piece of per-buffer state
  (cursors, scroll, find query, dirty). A missing/unreadable path
  starts empty with that path kept as the save target — the exact
  tolerance `main.cpp`'s own initial-launch handling already had
  (ADR 0006); opening a not-yet-existing file by name is a normal
  editor action, not an error, and this phase didn't invent a second
  standard for it.
- **`EditorViewport::saveAs`**: sets `m_filePath` then defers to the
  existing `save()` — dirty-clearing and the `statusChanged` emit
  happen in exactly one place, not duplicated.
- **`Ctrl+S` with no path set** now opens the Save-As panel instead of
  silently doing nothing (the gap ADR 0006 flagged). **`Ctrl+Shift+S`
  always** opens Save-As, even with a path already set — "save as"
  means "let me pick a different one."
- No overwrite confirmation for an existing file (v1 gap, documented
  rather than half-built — a real confirm flow is its own small
  floating panel, not a quick add-on).

### A real bug found and fixed during live verification

Both `FileBrowserPanel`'s path field and its list widget originally
handled `Return` through Qt's native mechanisms (`QLineEdit::
returnPressed`, `QListWidget`'s built-in Return-triggers-
`itemActivated`). Both are now handled directly in the shared
`eventFilter` instead, consuming the key press before Qt's native
handling ever runs. This wasn't a style preference — it fixed a real,
reproducible bug: `confirmPath()`/`activateEntry()` can end in
`hideBar()`, which moves keyboard focus to `EditorViewport`
*synchronously*, from inside the very key-press handling still in
progress. With the native signals, Qt's own event dispatch for both
widget types went on to redeliver that same logical key press to the
now-focused `EditorViewport` afterward — inserting a stray newline
into whatever file had just been opened or saved, and leaving it
marked dirty immediately after a clean load/save. `FindBar` never hit
this because its `Return` handling was already filter-based from
Phase 13, not signal-based — this ADR brings `FileBrowserPanel` in
line with that, and is the reason both floating panels now use the
same pattern.

## Consequences

Verified live via `xdotool`: status bar and window title track cursor
position and dirty state correctly through edits, `Ctrl+S`, and undo;
`Ctrl+O` opens the panel, keyboard navigation (`Tab` → `Down` ×N →
`Return`) descends into a subdirectory and back via `..`, and picks a
file — confirmed clean (no stray newline, no false-dirty title) only
*after* the Return-handling fix above; `Ctrl+Shift+S` with a typed
absolute path saves to a new file with exact content preserved;
launching with no file and `Ctrl+S` opens Save-As pre-populated with
the current working directory's listing. Full test suite unaffected
(this phase is GUI-only; no `core/` behavior changed) — 8/8 still
passing.

Not built: overwrite confirmation, a hidden-files toggle, breadcrumb
navigation beyond `..` (documented v1 gaps, none half-implemented).
Phase 15 (command line + `:compile` + output panel) is next.
