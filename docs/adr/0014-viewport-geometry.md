# ADR 0014: Viewport geometry — horizontal scroll, view-follow, line numbers

## Status

Accepted

## Context

Direct feedback identified a real gap, not just a polish request: the
viewport has never had horizontal scrolling. A long line just runs off
the right edge with nothing to reveal it, and typing past the visible
width leaves the cursor invisible with no way to see what you're
typing. Separately, there's no line-number gutter — something the
original spec's aesthetic section (section 2) already anticipated
("line numbers, bracket/indent guide lines, gutters... visually
de-emphasized, not competing for attention") but no phase ever built.

Both land in one pass because they change the same thing: text no
longer starts flush at widget x=0. Doing that plumbing once and hanging
both features off it is less work and less risk than two passes that'd
each half-touch the same rendering code.

## Decisions

### 1. Horizontal scroll lives in pixel space, not column space

`m_scrollX` is the leftmost visible *pixel*, not a column count.
ADR 0013 already established that column-based pixel math drifts
(font glyphs aren't perfectly uniform width) — reusing that mistake for
scroll position would just relocate the same bug. `xForColumn`
(`gui/src/editor_viewport.cpp`) already gives exact pixel positions per
column; scroll state uses that same currency.

### 2. One `QPainter::translate` + `setClipRect`, not per-call arithmetic

`paintEvent` wraps the existing per-line `drawLine` loop and the caret
loop in `painter.save() / translate(gutterWidth() - m_scrollX, 0) /
setClipRect(...) / ... / restore()`. Neither `drawLine` nor the caret
positioning code needed to change at all — they already compute exact
local pixel positions via `xForColumn`; the transform handles gutter
offset and scroll together. The gutter itself is drawn *after*
`restore()`, back in untranslated widget space, so it always paints
cleanly over the left margin regardless of scroll position. This is
less code and less risk than threading a `gutterWidth() - m_scrollX`
offset through every x computation by hand.

### 3. View-follow is symmetric to the existing vertical logic

`ensureCursorVisible` already had a vertical half (`m_scrollLine`
clamps to keep the cursor's line in view). Decision here is simply:
give it a horizontal half with the same shape — compute the reference
cursor's exact pixel x via `xForColumn`, scroll left if it's before
`m_scrollX`, scroll right if it (plus the caret's own width) would
fall past the visible text width. Same clamp-based approach, no special
casing for Home/End — Home naturally produces `caretX = 0`, which is
always less than any positive `m_scrollX`, so the existing "scroll
left to reveal" branch handles it for free.

### 4. Line numbers: on by default, absolute or relative, one dimming
tier reused from ADR 0012

`line_numbers` config key: `off` / `absolute` / `relative` (default
`absolute` — per the spec's own aesthetic section, line numbers are an
expected de-emphasized element, not an opt-in extra; "optional" per the
request means *configurable*, not *off by default*). Relative mode
shows `abs(line - cursorLine)` for every line except the cursor's own,
which always shows its absolute number — standard Vim convention, and
relative to the *last* cursor when multiple are active, matching how
`ensureCursorVisible` already picks a reference cursor for multi-cursor
(ADR 0012).

Gutter text reuses the comment-dimming alpha from ADR 0012 (145/255,
already measured at 4.91:1 against the background — comfortably WCAG
AA even though gutter chrome likely only needs the lower "UI
component" bar) for every line except the cursor's, which renders at
220/255 (9.29:1) to stand out as the current line. Reusing an
already-verified tier instead of inventing a third dimming value.

Gutter width is measured, not assumed: `QFontMetrics::horizontalAdvance`
on the actual widest line-number string, not digit-count × a fixed
per-digit width — same reasoning as ADR 0013, applied to new code
instead of retrofitted.

## Consequences

`offsetForPoint` (mouse click → offset) now subtracts
`gutterWidth() - m_scrollX` before its existing approximate column
lookup — still deliberately approximate per ADR 0013, just shifted.
Wheel scrolling stays vertical-only; horizontal movement is
cursor-follow only, matching what was actually requested (view follows
the cursor, not a horizontal scrollbar/wheel gesture). Smooth
animation of these transitions (rather than the instant clamp-snap
here) is deliberately deferred to the next phase, which depends on
this one's exact-pixel-position plumbing being in place first.
