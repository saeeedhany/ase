# ADR 0041: Diagnostic underline redesign — thin line, dim/focus states

## Status

Accepted

## Context

User feedback on the diagnostics UI, two parts:

1. The wavy zigzag underline under diagnostic ranges (`drawSquiggle`,
   `docs/adr/0029`) should be a plain thin straight line instead.
2. Diagnostics should be dim/subtle by default (visible but not
   competing with everything else on screen) and brighten to full
   opacity specifically on the line the cursor is currently on, with a
   smooth, fast, left-to-right wipe transition rather than an instant
   swap — "the effect of a moved line." The gutter dot gets the same
   dim/focused distinction, but as a plain fade, no wipe.

An earlier pass at this (mid-implementation) misread the second part
as a full-row background wash spanning the whole line's width — built,
verified working, and then corrected: the request was about the
diagnostic *underline* itself, confined to its own span, not a
whole-line highlight. That version was reverted before landing.

## Decision

### `drawSquiggle` → `drawDiagnosticUnderline`

Same per-line segmentation and `xForColumn`-based x0/x1 measurement as
before (so it still lines up exactly with the actual rendered glyphs —
docs/adr/0013's measurement discipline). Renders a plain
`QPainter::drawLine` instead of building a zigzag `QPainterPath`.

### Dim by default, wipe to focused

Two states, both drawn every frame the diagnostic's line is visible:

- **Dim**: the full `[x0, x1]` span, drawn at a fixed low alpha
  (`kDiagnosticUnderlineDimAlpha = 110`) — always present on any line
  with a diagnostic, whether focused or not.
- **Focused overlay**: drawn on top at full alpha
  (`kDiagnosticUnderlineFocusAlpha = 255`), but only across
  `[x0, x0 + (x1-x0) * reveal]` — `reveal` is an eased 0..1 value that
  animates toward 1 while the cursor sits on that line, and back toward
  0 once it doesn't (a real, verified reverse-wipe: the bright segment
  visibly recedes back toward `x0`, mirroring how it grew in).

The gutter dot (already existing, `docs/adr/0029`) gets the same
`reveal` value, but drives a straight alpha interpolation between
`kDiagnosticDotDimAlpha = 150` and `kDiagnosticDotFocusAlpha = 255` —
no wipe, just a fade, per the "same thing for the dot but just to be
focused" request.

### Shared animation state: `DiagnosticLineHighlight`

A small `QVector<DiagnosticLineHighlight>` (`{line, reveal, target}`)
tracks at most a couple of lines at once — the one wiping in, and the
one that just lost focus and is wiping back out.
`updateDiagnosticLineHighlights()`, called every frame from
`updateAnimation()` (unconditionally, even with `animations = false`,
where it just snaps `reveal` straight to `target` — the same "state is
always live, only the transition's smoothness is opt-in" convention
`paintEvent`'s existing current-line-number brightness already uses),
sets `target = 1` for whichever line the primary cursor
(`m_cursors.last()`, same "primary cursor" convention used elsewhere)
is on, if that line has a diagnostic, and `target = 0` for every other
tracked line. Reuses `kEaseFactor` (the same constant driving caret
glide and scroll easing, docs/adr/0027) so the motion feels consistent
across the whole editor, and drops an entry once it settles back at 0
so the vector never grows unbounded.

`worstSeverityForLine()` was factored out of the gutter-dot loop
(previously inline) since both the underline and dot passes now need
it.

### Verified live, including mid-animation

Opened a real `.c` file with clangd wired up and genuine compiler
diagnostics (an undeclared function call, a missing semicolon).
Confirmed: both diagnostic lines show a dim thin underline and a dim
gutter dot at rest; clicking one wipes its underline to full brightness
left-to-right (caught mid-wipe in a screenshot — a partial bright
segment with the dim remainder still visible past it) and its dot pops
to full opacity; clicking away reverses both, caught mid-collapse in
another screenshot showing the bright segment receding back toward the
line's start rather than just vanishing.

## Consequences

Diagnostics are now easier to visually scan at rest (a plain thin line
reads calmer than a zigzag across a whole codebase full of warnings)
while still drawing the eye to whichever one the cursor is actually
on. No new timer: the effect piggybacks on the existing blink-timer-
driven repaint loop, like every other eased animation in this file.
