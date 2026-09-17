# ADR 0123: A range that ends where a line begins

## Status

Accepted

## Context

Reported after ADR 0122 fixed where the linewise cursor sits: pressing
`V` highlighted the line correctly, and left a thin vertical mark under
it — narrower than a character, on the row below, looking like a
selection that was not one.

## Decision

`highlightRange()` walks `lineForOffset(start)` to `lineForOffset(end)`
inclusive. The end is **exclusive**, so a range finishing exactly at a
line's first byte still counted that line as its last.

For such a line the computed span is empty — `rangeStartCol` and
`rangeEndCol` are both zero — and the fill was guarded by
`std::max(1, rectWidth)`, which turned nothing into one pixel of full
line height at the left edge of the text.

A linewise selection ends at the start of the following line *every
time*, so it produced this on every use of `V`.

The fix is to skip a line the range does not reach into, rather than to
drop the `max(1, ...)`, which is still wanted for a real span narrower
than a pixel.

## Consequences

Measured rather than eyeballed, by diffing the frame with `V` pressed
against the same frame without it: the row below the selection had
exactly one column changed at x=25 before, and none after.

The bug predates ADR 0122 — a linewise range has always ended at the
next line's start. It only became visible once the cursor stopped
sitting on that line, because the caret was drawn over the same pixels
and hid it.

The `+ m_charWidth / 2` on lines before the last one still marks the
selected newline, which is the deliberate cue that a linewise selection
takes the line break with it.
