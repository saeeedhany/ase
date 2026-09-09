# ADR 0013: Fix caret drift — measure rendered text, don't assume a fixed pitch

## Status

Accepted

## Context

User-reported bug: typing steadily on one line, the caret visibly
drifted away from the actual insertion point — the more characters
typed, the bigger the gap between the blinking caret and where the
next character actually appeared.

Root cause: every position on screen (the caret's x, and each syntax-
highlight run's starting x in `drawLine`) was computed as
`column * m_charWidth`, where `m_charWidth` is
`QFontMetrics::horizontalAdvance('M')` — the width of one specific
glyph, measured once. That's an assumption that the font is perfectly,
uniformly fixed-pitch at the pixel level. It mostly is, but not
exactly: Qt's actual text shaping (hinting, antialiasing, subpixel
positioning) doesn't always lay out a 60-character string at exactly
60× that one glyph's measured width. The per-character error is
sub-pixel, so it's invisible for a few characters — but it accumulates
linearly with line length, which is exactly the "starts slowly, gets
worse" symptom reported. Reproduced directly: typing 50+ characters on
one line, the caret ended up rendering several character-widths past
where the text actually ended.

## Decision

Stop assuming; measure. Caret position and inter-run advance are now
both computed from `QFontMetrics::horizontalAdvance(actualRunText)` —
the same text-shaping computation Qt's own `drawText` uses internally
— rather than `column * m_charWidth`. Concretely:

- `EditorViewport::xForColumn(lineStart, lineEnd, column)` walks the
  same per-capture-run segmentation `drawLine` already uses
  (factored out into `capturesForLine`, shared by both) and sums each
  run's *measured* width up to `column`, using **that run's actual
  font** (bold for keywords, italic for types — a bold run is visibly
  wider than the same text at regular weight, so measuring with the
  base font alone would reintroduce a smaller version of the same
  bug on syntax-highlighted lines). The caret (`paintEvent`) and
  `drawLine`'s own inter-run advance both call this same measurement
  path now, so the two can never disagree with each other the way the
  old column-multiplication and the old column-multiplication
  disagreed with what Qt actually painted.
- `m_charWidth` still exists and is still used in exactly one place:
  `offsetForPoint`'s pixel→column mapping for mouse clicks. Left as an
  approximation deliberately — a click landing a character off on a
  long or heavily-styled line is a minor, one-time miss, not a growing
  visible bug, and doesn't justify an O(line length) exact-measurement
  scan on every click the way the caret's *continuous* drift did.

## Consequences

`drawLine` and the caret now share one source of truth for "where is
column N on screen," so they cannot drift apart from each other by
construction — the bug class (two independent approximations of the
same quantity, one used for painting and one for the caret, agreeing
only by luck) is closed, not just patched for this one report. The
`applyCaptureStyle` function this replaced is gone; `fontForCapture` /
`colorForCapture` are the two pieces it was doing at once, now
reusable independently (measurement needs the font; painting needs
both).

Verified by reproducing the original bug (typing 50+ characters,
screenshotting, confirming the caret visibly detached from the text),
then confirming the fix on the same reproduction (caret stays
pixel-adjacent to the last character), and additionally on a `.c` file
with bold/italic syntax-highlighted runs on the same line the new text
was appended to.
