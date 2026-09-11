# ADR 0039: Fix mouse click/hover drift on styled lines

## Status

Accepted

## Context

User-reported bug: clicking (or hovering) further right on a line
increasingly missed the character actually under the pointer, on
lines with mixed syntax-highlight styling (e.g. bold keywords next to
plain identifiers). This is the same class of bug ADR 0013 already
fixed for the *caret* — assuming a fixed per-character pixel width
instead of measuring what was actually rendered — just never fixed on
the mouse-hit-testing side.

`EditorViewport::offsetForPoint()` (used by both `mousePressEvent` and
the no-button branch of `mouseMoveEvent`, i.e. click, drag-select, and
hover) computed the target column as `localX / m_charWidth`, a single
project-wide average character width. Meanwhile `drawLine` and
`xForColumn` — used for actually painting text and positioning the
caret — measure each syntax-highlight run's real advance with
`QFontMetrics::horizontalAdvance` on that run's actual text and font
(`fontForCapture`/`metricsForCapture`), because bold/italic variants of
even a "monospace" font don't reliably share the exact same advance as
the regular weight. Any line with more than one highlight capture
accumulates a small error per run boundary; far enough right on a long
styled line, that error becomes visible as exactly what was reported —
the pointer needing to move noticeably further than a character-width
to advance the cursor by one character, or landing on the wrong one
entirely.

## Decision

Added `EditorViewport::columnForX(lineStart, lineEnd, localX)`, the
direct inverse of `xForColumn`: it walks the same per-run segmentation
`xForColumn`/`drawLine` use, measuring each run's actual rendered
width, and finds which column's glyph `localX` falls nearest to —
rounding to the *nearest* codepoint boundary rather than flooring
(same reasoning as ADR 0028's click-precision fix), and stepping by
codepoint (using the existing `isUtf8ContinuationByte` helper) rather
than by raw byte, so a multi-byte UTF-8 character is never measured as
a partial, invalid byte sequence.

`offsetForPoint()` now calls `columnForX()` instead of dividing by
`m_charWidth`. This also *removes* code, not just adds it: the
separate post-hoc "snap forward past any continuation byte" loop
`offsetForPoint()` used to need is now redundant — `columnForX()`
already only ever returns a codepoint boundary.

`m_charWidth` itself is unchanged and still used elsewhere (horizontal
scroll margin, the line-wrap continuation hint) — those are approximate
by nature already, unlike hit-testing where the pointer needs to track
a specific glyph.

### Verified live, not just built

Launched the real GUI against a `.c` file with a long line mixing bold
keyword highlights and plain identifiers. Placed the caret at a known
column via keyboard navigation (`End`, then `Left` N times),
screenshotted its exact pixel position, then clicked at that exact
pixel and confirmed the reported column matched — both near column 80
on a ~100-column styled line and again near the line's end. Before
this fix, `offsetForPoint`'s fixed-pitch math and `xForColumn`'s
per-run measurement could only coincidentally agree; after, a mouse
click and the caret's own rendered position resolve to the same
character by construction, however far right on the line.

## Consequences

Click, drag-select, and hover (LSP hover popup, completion popup
positioning) all route through `offsetForPoint`, so all three are
fixed together, not just the plain click case reported. No new O(line
length) cost class was introduced beyond what already existed:
`xForColumn` was already a linear scan over the line's runs for the
caret's own position, and `columnForX` is the same shape.
