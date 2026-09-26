# ADR 0144: Select the line, not the line and a bit

## Status

Accepted

## Context

`Shift+V` highlighted half a character past the end of every selected
line. Reported twice before it was looked at properly, both times as
"it takes a small space beside it" — which is exactly what it was, and
the measurement afterwards said `m_charWidth / 2` to the pixel.

## Decision

The half cell is a charwise hint. `highlightRange()` adds it to every
line of a range except the last, where it says the line break is inside
the selection — worth saying when only *some* breaks are.

A linewise selection includes every break by definition, so the hint
carries no information there. What it does instead is end each row at a
different offset past its own text, which is the ragged edge that kept
getting reported.

So the hint is charwise only.

### The one nobody reported

Looking at it turned up a second bug in the same function: an empty line
inside a linewise selection got **no highlight at all**. It has no
characters to cover, so it hit the guard that skips a line a range does
not reach, and a five-line selection over two blank lines drew as three
separate selections.

An empty line in a linewise selection now gets a cell of its own. Vim
does the same, for the same reason.

### Painting split from the shape

The per-line rectangles are computed by `highlightRects()` and filled by
`highlightRange()`, so the shape of a selection can be asserted on
rather than only looked at.

That split is the actual lesson here.
[ADR 0123](0123-a-range-that-ends-where-a-line-begins.md) fixed a
one-pixel sliver in this same function by eye, and both of these were
sitting underneath it the whole time — one of them for every linewise
selection ever made. A function whose only verification is a screenshot
gets checked once, for the thing being changed that day.

## Consequences

Five tests over the rectangles: that a linewise line stops at its last
character while the charwise one does not, that the last line of a range
never gets the hint in either mode, that an empty line is painted
linewise and skipped charwise, and ADR 0123's original case. Reverting
either fix fails two of them.

One assertion of mine was wrong twice while writing them, in a way worth
recording: a rect's width is the *measured advance* of the text, not
`m_charWidth` times the character count. `m_charWidth` is one glyph's
advance — `M` — and the offscreen font is not exactly monospaced, so
"short" measures 48 where five cells would be 50. The test asserts the
difference between the two modes instead, which is the thing the change
is actually about.
