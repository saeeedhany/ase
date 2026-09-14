# ADR 0072: Incremental highlighting, and a measured hot path

## Status

Accepted

## Context

"Fast as hell" is the first claim the project makes about itself.
ADR 0053 measured and fixed one hot path (22% idle CPU → 0.0%) and left
"incremental Tree-sitter reparse" as the largest remaining gap, with an
estimate of ~12.7ms per keystroke on an 8400-line file.

The estimate was optimistic by an order of magnitude. Measured on a
generated 10,803-line C file (276KB), with the editor instrumented and
driven through real X11 keystrokes:

| per keystroke | ms |
|---|---|
| full Tree-sitter reparse | **107** |
| query over the whole tree — 14,402 spans, to draw ~45 lines | **64** |
| tree delete | 3 |
| rebuilding line starts | 4.4 |
| flattening captures | 2 |
| **total `refreshCache()`** | **~177** |

Every keystroke. On a file a tenth the size of the kernel's larger
sources.

## Decision

### The parse is incremental, and the edit is derived, not threaded

`AseSyntax` keeps its tree and a copy of the text it was parsed from.
On the next call it derives the edit — common prefix, common suffix,
everything between them is what changed — builds a `TSInputEdit`, calls
`ts_tree_edit()`, and hands the old tree back to the parser.

The alternative was an API that takes the edit, which would mean every
one of the ~15 mutation sites in `EditorViewport` computing byte offsets
*and* row/column points and getting them right. Deriving it from two
buffers costs one pass and keeps `ase_syntax_highlight()`'s "here is the
whole text" contract, so no caller changes at all.

This is deliberately not an exact edit list: several disjoint changes
(a multi-cursor edit, an undo restoring scattered text) collapse into
one span covering all of them. That is *correct* — a `TSInputEdit` may
describe more than actually changed, it only costs a larger reparse —
and it is the reason the simple version is also the safe one.

The three points a `TSInputEdit` needs are computed in one pass rather
than three: find the start point from the beginning of the file, then
advance from there to each end point, since both are at or after it.

Any path that cannot prove the stored text matches the stored tree
throws both away and parses from scratch. A wrong incremental parse
corrupts highlighting in ways that look like a Tree-sitter bug.

### The query runs over a window, not the file

A syntax tree of half a file is not a syntax tree, so the parse still
covers everything. But the *query* that turns the tree into spans only
has to answer for what is on screen. `ase_syntax_highlight_range()`
takes a byte range; the viewport keeps a capture window padded a
screenful either side, recomputes it when an edit invalidates it, and —
this is the part that is easy to get wrong — re-checks it in
`paintEvent()`, so scrolling into unqueried text cannot draw unstyled.

14,402 spans per keystroke became 272.

### The line scan was slower than the parser it sat next to

Rebuilding `m_lineStarts` with a byte loop over a `QByteArray` measured
**8.2ms** for 276KB — 34 MB/s, and more than the incremental parse it
runs beside. `memchr` plus `resize(0)` (which keeps the vector's
capacity, unlike `clear()`) took it to 1.7ms.

## Consequences

Same file, same measurement method (process CPU time across 57 real
keystrokes):

| | ms/keystroke |
|---|---|
| before | **52.1** |
| after | **11.2** |

and `refreshCache()` itself went from ~177ms to ~7ms: 1.7ms line scan,
~3.4ms incremental parse, ~1.3ms deriving the edit, ~2ms query and
flatten.

Steady-state idle, animations on, same file: **9%** of one core, and
paint accounts for all of it (2.4ms × 36fps). With animations off: 1%.

Correctness checked live rather than assumed: typing at the end of a
10,803-line file highlights correctly; jumping to line 5000 and back to
line 1 highlights correctly (the window follows); inserting `unsigned `
into a declaration and then undoing it leaves every keyword, type,
string and comment correctly coloured — insertion and deletion both
exercise the derived edit.

**An honest note on a number I could not pin down.** Early samples on a
freshly-opened large file showed ~90% CPU at idle, both before and after
these changes. Once the file had been opened a few times it settled to
the 9% above and never returned, so the likeliest explanation is
first-open work — the initial full parse, the language server starting
and its first diagnostics — rather than a steady-state leak. It is
recorded here because it was measured, not because it is understood.

### Two cleanups that came out of the survey

- **`-Wall -Wextra` is now enforced.** The project builds clean at those
  flags and always has; nothing was *asking* for them, so nothing would
  have stopped the next warning. They are carried by an `ase_warnings`
  INTERFACE target linked into the three first-party targets, so the
  Tree-sitter and Lua sources FetchContent pulls in — which are not ours
  to keep clean — are unaffected.
- **Panel text-field theming is one helper.** Four panels each set the
  same five palette roles by hand, and the placeholder fix from ADR 0055
  was present in two of them and missing from the other two — which is
  what a copied idiom eventually looks like. `SmoothLineEdit::applyPanelTheme()`
  now owns it, and every field gains the next fix at the same time.

`ctest` 9/9, clean build, zero warnings.

### What is next, in measured order

1. **Repaint the caret's rect, not the whole viewport.** Idle cost is
   now entirely a full-window repaint 36 times a second to animate one
   blinking caret. Rect-limited updates would take that 9% to nearly
   nothing.
2. **Incremental line starts.** 1.7ms per keystroke is a whole-buffer
   scan to learn something an edit already knows: lines before the edit
   are unchanged, and the rest shift by a known delta.
3. The `ase_syntax` text copy doubles the buffer's memory. Fine at
   276KB, worth revisiting alongside a real large-file pass.
