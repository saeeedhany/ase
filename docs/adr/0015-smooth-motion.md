# ADR 0015: Smooth motion — caret glide and smooth scroll

## Status

Accepted

## Context

Direct feedback: the caret blink and cursor/scroll movement felt
abrupt, not "alive." Phase 7 (ADR 0012) already added an opt-in caret
alpha-fade but explicitly deferred smooth scrolling as too complex for
that phase's budget — it needed exact pixel positions to animate
against, which didn't exist until Phase 8's caret-drift-safe
`xForColumn` and pixel-space `m_scrollX`/`m_scrollLine` (ADR 0013,
ADR 0014). This phase spends that plumbing.

## Decisions

### 1. One easing mechanism drives both scroll and caret position

`m_scrollLine`/`m_scrollX` stay the *logical* target — nothing that
sets them (`wheelEvent`, `ensureCursorVisible`) changed at all. New
`m_renderedScrollLine`/`m_renderedScrollX` (doubles) hold what's
actually drawn, eased toward the logical target by a fixed fraction
(`kEaseFactor = 0.35`) per paint, snapping once within a small
threshold. Each cursor gets the same treatment via
`m_renderedCaretPos` (one `QPointF` per cursor) eased toward
`caretTargetFor(cursor)` — which itself is computed *against the
current rendered scroll position*, not the logical target, so a caret
that isn't otherwise moving stays visually anchored to its character
while the view glides underneath it during a scroll animation, instead
of being dragged along with the logical (instant) target.

### 2. Advance the animation from `paintEvent`, not the timer

The existing 30ms `m_blinkTimer` (unchanged interval, unchanged role —
still just "call `update()` periodically so something keeps
happening") triggers repaints; `updateAnimation()` runs at the *top* of
`paintEvent` itself, immediately before its results are read. This
sidesteps any staleness question entirely — there's no call site
(`keyPressEvent`, `mousePressEvent`, the config-reload path) that needs
to remember to advance the animation before triggering a repaint;
whichever code path calls `update()`, the next `paintEvent` always
advances state right before drawing it.

### 3. Cursor-count changes resync instead of gliding from mismatch

`m_renderedCaretPos` has to stay parallel to `m_cursors`, which can
change size at several call sites (mouse click, `Alt`+click, `Ctrl+D`,
`Escape` — all from ADR 0012). Rather than updating the array at each
of those sites, `updateAnimation` just checks size equality each frame
and, on mismatch, rebuilds it at exact target positions (no glide for
that one frame) before continuing. Simpler than threading a resync
call through every multi-cursor mutation point, and the cost — a newly
added cursor appears rather than gliding in from nowhere — is the
right behavior anyway.

### 4. The one real complexity this phase actually needed: fractional
vertical rendering

A fractional `m_renderedScrollLine` means the topmost visible line is
only partially scrolled into view. `paintEvent` renders from
`floor(m_renderedScrollLine)`, shifts the whole per-line loop up by
`frac * m_lineHeight` via the same `painter.translate` that already
carries the horizontal scroll/gutter offset (ADR 0014), and requests
one extra line at the bottom (`height()/lineHeight + 2`, not `+ 1`) so
nothing blank slides into view during the shift. The gutter's line
numbers get the identical vertical shift (a second, vertical-only
`translate`) so they stay aligned with their text lines throughout.
Horizontal easing needed none of this — `translate`'s x argument is
just a `double`, fractional pixels for free.

### 5. Carets render in absolute widget space, decoupled from the
translated text block

Because a caret's rendered position can lag the logical scroll target
(mid-glide), it can no longer be drawn as "a local-space x/y inside the
same translate as the text" the way Phase 8 had it — that would force
it to jump instantly with any scroll change. Carets are now drawn
after `painter.restore()`, directly at their `m_renderedCaretPos`
(already computed as absolute widget coordinates in `caretTargetFor`),
clipped to the text area so a caret scrolled behind the gutter doesn't
paint over it.

### 6. Click mapping (`offsetForPoint`) uses rendered, not logical, scroll

A click during an active scroll animation has to map against what's
actually on screen at that instant. `offsetForPoint` now reads
`m_renderedScrollLine`/`X` instead of the logical values — vertically
only to `floor()` precision, consistent with the pre-existing
deliberate click-approximation philosophy (ADR 0013).

## Consequences

`animations = false` (the shipped default) makes `updateAnimation`
snap every rendered value to its target every frame — verified to
produce pixel-identical consecutive frames after a jump, i.e. true
instant behavior, not just a fast ease. Verified with `animations =
true` that a large scroll jump visibly interpolates over several
frames (sampled actual line-number-gutter content across rapid
screenshots showing 232 → 237 → 238 converging toward the true target)
rather than jumping.
