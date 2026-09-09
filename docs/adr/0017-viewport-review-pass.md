# ADR 0017: Viewport review pass — four fixes from live use

## Status

Accepted

## Context

Continued user testing surfaced four distinct problems in quick
succession, all in the rendering/animation code added across Phases
8–9 and the ADR 0016 fix. Rather than patch each in isolation, this
was a full review of that code's interactions — the request was
explicitly for real fixes that wouldn't regress anything else, not
quick patches.

## Decisions

### 1. Invisible text past the horizontal scroll point — the most
severe bug

`drawLine`'s `drawText` call used `QRect(x, y, width() - x, m_lineHeight)`.
Once ADR 0014 introduced horizontal scroll, `x` is a *local* (translated)
coordinate that legitimately exceeds the widget's raw `width()` on any
sufficiently long, scrolled line — `width() - x` goes negative, and
Qt's `drawText` paints nothing into a negative-width rect. Typed
characters past that point rendered as blank space — a real, severe
regression from ADR 0014, not cosmetic.

Fixed by using the run's own measured width (`runWidth`, already
needed for the x-advance) as the rect's width instead: always exactly
right, never dependent on how far into a line the viewport happens to
be scrolled. Reproduced directly (typed ~300 characters past the
overflow point, confirmed text visible throughout) before and after.

### 2. Caret still visibly blinking/fading while moving, with
animations on

ADR 0016 fixed this for the hard blink but missed that the animated
fade used a *different*, free-running counter (`m_caretTick`,
incremented unconditionally every tick regardless of activity) — so
with `animations = true`, the fade kept oscillating through movement
exactly as before, which is what the user was still seeing and
describing as "blinking while I move it."

Fixed by deleting `m_caretTick` and driving the fade's phase from the
same `m_idleTicks` the hard blink already uses — reset to 0 by
`resetCaretBlink()` on every cursor-moving action. Switched `sin` to
`cos` for the phase function specifically so that the instant something
goes idle (`m_idleTicks == 0`) evaluates to full brightness — the fade
now always starts from "was solid" and eases into the breathing cycle,
rather than jumping to an arbitrary point on the curve the moment
movement stops.

### 3. Caret (and view) feeling like they lag during fast typing/deleting

Root cause was really a design gap, not a bug: `updateAnimation`'s
easing (`kEaseFactor` per paint, ADR 0015) is tuned for deliberate
navigation jumps (arrow keys, clicks, `Ctrl+D`), where a visible glide
reads as smooth. During *fast, repeated, small* jumps — typing or
holding delete — each new keystroke retargets the ease before the
previous step finishes, so the rendered caret perpetually chases a
moving target and never actually reaches it. That reads as "the editor
can't keep up," which is the opposite of the intended effect and a
direct hit against the "fast as hell" pillar.

Fixed by adding `snapAnimationToTarget()` (forces every rendered value
to its exact target, bypassing easing for that one update) and calling
it after every text-mutating `keyPressEvent` branch — typing, Enter,
Backspace, Delete — regardless of `animations`. Pure navigation
(arrows, Home/End, click, `Ctrl+D`) is untouched and keeps the glide.
This is the same logic `updateAnimation`'s disabled-animation branch
already needed, so it's factored out and shared rather than duplicated
— see decision 4's fix for why that sharing mattered.

### 4. `QFontMetrics` reconstruction, not just a feeling

While reviewing the above, found `drawLine`, `xForColumn`, and
`caretTargetFor` (the last two called once per cursor on *every*
animation tick) were each constructing a fresh `QFontMetrics(font)` for
every styled run, every call — never cached. `QFontMetrics` construction
queries the font engine; doing it dozens of times per frame, every 30ms
while animating, is real avoidable work sitting directly in the path
the "feels slow" reports were pointing at. Added three cached instances
(`m_metrics`, `m_boldMetrics`, `m_italicMetrics`) built once in
`applyConfig()`, fetched via `metricsForCapture()` everywhere a font's
metrics are needed. Purely a reuse-not-reconstruct change — the
values were already being computed correctly, just repeatedly.

## Consequences

`updateAnimation`'s "animations off" branch and the new
`snapAnimationToTarget()` are now one function, not two copies of the
same four lines — the earlier ADR 0016 bug (clearing instead of
populating `m_renderedCaretPos`) was exactly the kind of mistake
duplication invites, so this pass consolidated rather than added a
third copy. All four fixes verified live: overflow text visible
arbitrarily far into a scrolled line; caret stays solid through a
sustained sequence of arrow-key presses with animations on; a fast
typed burst shows every character landing at full brightness with the
caret exactly adjacent, no trailing gap. Full test suite unaffected
(these bugs were all in GUI rendering/animation state, outside
anything the suite covers) and still green.
