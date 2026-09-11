# ADR 0043: Faster global easing; welcome overlay two-column layout

## Status

Accepted

## Context

Two pieces of direct UI-polish feedback:

1. Caret movement on Enter/Delete and general navigation felt slow —
   not the blink (already fine), the *glide* to a new position.
2. The welcome overlay's shortcut list (docs/adr/0042), a single
   flat centered line per shortcut, read as visually disorganized.

## Decision

### `kEaseFactor`: 0.5 → 0.68

`kEaseFactor` was already the single constant every eased value in
`editor_viewport.cpp` multiplies its remaining distance by each frame
— caret glide, scroll, diagnostic focus reveal (docs/adr/0041), and
the welcome overlay's own fade (docs/adr/0042). Raising it speeds up
all of them together, by construction, with a one-line change — the
"make it global" request was already satisfied by the existing
architecture; this just raises the value. Remaining-distance-per-tick
drops from `0.5^n` to `0.32^n`, roughly halving the number of frames
to visually settle. Third bump on this constant (0.35 → 0.5,
docs/adr/0027 → 0.68 here) — motion feel is inherently something to
judge live, not from a screenshot, so this is reported for the user to
confirm during actual use rather than claimed as independently
verified.

### Welcome overlay: two-column, dot-led layout

Replaced the flat `"Ctrl+O — Open a file"` centered string per line
with a `{description, key}` pair rendered as: a small bullet dot,
then the description left-aligned, then the key right-aligned — column
widths measured from the actual content (`QFontMetrics::horizontalAdvance`,
not guessed), so it stays aligned regardless of font/size. The whole
block is still centered under the logo; only the *internal* alignment
of each row changed. Verified live via screenshot — the four rows now
read as a clean, legend-style list rather than four independently
centered strings.

## Consequences

No new state, no new timer — both changes are edits to existing code
paths (a constant, and one drawing function's layout math). All 9/9
tests still pass (unrelated to this GUI-only change, re-run as a
sanity check regardless).
