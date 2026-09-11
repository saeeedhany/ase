# ADR 0040: Fix italic glyphs clipped at the top

## Status

Accepted

## Context

User-reported bug: an italic word inside parens like `(int)` looked
like its top was "eaten." In practice this was worse than cosmetic —
italic `void` was rendering legibly as `voia`: the ascender of the
italic `d` was sheared off, leaving just the rounded bowl, which reads
as an `a`.

`drawLine()` draws each syntax-highlight run with
`painter.drawText(QRect(x, y, runWidth, m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter, text)`.
`m_lineHeight` is set once, in `applyConfig()`, from the *plain*
font's metrics (`m_metrics.height()`) — never recomputed per style.
`QPainter::drawText(QRect, ...)` clips text to the given rectangle by
default (`Qt::TextDontClip` is not set). Italic (and to a lesser
extent bold) variants of the same font can report a taller ascent than
the plain variant — real for most fonts, and closer to guaranteed for
a synthetic/oblique italic that some platforms fall back to when a
font has no true italic face. Centered with `Qt::AlignVCenter` inside
a box sized for the plain font, an italic run's ascender extends above
the top edge of that box and gets clipped.

## Decision

Added `Qt::TextDontClip` to both `drawText` calls in
`editor_viewport.cpp` (the per-run text draw in `drawLine()`, and the
gutter line-number draw, for the same defensive reason even though
plain digits are unlikely to trigger it in practice). The per-run
rect's job was always alignment/positioning, not clipping — the outer
`paintEvent()` clip region (`docs/adr/0014`) already keeps painting
inside the viewport, so there was never a reason for a second, tighter
clip at the per-run level.

### Verified live

Rebuilt and opened a real `.c` file with `int main(void) {` and
`(int)3.5` on screen. Before the fix, italic `void` visibly read as
`voia`. After: both `void` and the `int` inside the cast render with
their full, uncut glyphs, confirmed by a zoomed screenshot crop.

## Consequences

No downside found: nothing in this codebase relied on per-run clipping
(a run's rect width already exactly matches its own measured advance,
so there's no overflow to clip horizontally either — this only ever
affected the vertical case). All 9/9 core tests still pass (unrelated
to this GUI-only change, but re-run as a sanity check).
