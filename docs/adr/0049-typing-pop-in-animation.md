# ADR 0049: A typing pop-in animation

## Status

Accepted

## Context

The last item from the same round of feedback that produced ADR 0048
("add an animation while typing to give a good effect") was
deliberately left undesigned there — unlike the other three requests
in that batch, it had no single obvious shape. Two concrete directions
were put to the user: animate each newly typed character (fade + scale
from ~85% to 100%), or give the caret itself a pulse on each keystroke.
The user picked the per-character pop-in.

## Decision

### Where it hooks in: `insertText`/`insertTextAt`, not a new call site

`insertText()` is the one funnel every "typing-shaped" insertion already
goes through — plain character entry, Tab, Enter, paste, find/replace's
replacement text, and completion-accept all call it. Rather than adding
a separate "this was really typed" signal, the animation is gated by
the inserted bytes themselves: `bytes.size() <= kTypingAnimationMaxBytes
(8) && !bytes.contains('\n')`. In practice this means plain characters
(including a multi-byte UTF-8 one) and Tab's four spaces animate; a
paste, a multi-line completion insert, or anything containing a newline
does not — a real paste popping in as one giant scaled block would look
wrong, not good, and Enter's own `'\n'` has no visible glyph to animate
in the first place. Also gated on `m_animationsEnabled`, same opt-in
pattern as every other animation in this file — with animations off,
`insertTextAt`'s `animate` parameter is always false and
`m_typingAnimations` never grows.

`insertTextAt(int i, const QByteArray &bytes, bool animate)` gained the
one new parameter; on a successful `ase_buffer_insert`, it pushes
`{cursor, bytes.size(), 0}` (the pre-increment cursor offset is exactly
the insertion's start) into `m_typingAnimations` before advancing
`cursor` past the inserted bytes. Multi-cursor typing is already safe
by construction: `insertText()`'s existing loop processes cursors
highest-offset-first specifically so an earlier iteration can never
write before a later one's position — the same invariant the file's
delete primitives already rely on (docs/adr/0012) — so each pushed
`start` offset stays valid for the rest of that call without needing
any adjustment for the other cursors' insertions.

### Rendering: repaint over the glyph, don't touch `drawLine`'s segmentation

`drawLine` already painted these bytes at full size and opacity by the
time the animation overlay runs (same frame). Rather than teaching
`drawLine`'s per-capture run segmentation about a third, transient
"still popping in" state, the overlay pass (right after the main text
loop, same translate/clip context) re-paints just that byte range's
rect: fills it with `m_backgroundColor` first (erasing what `drawLine`
already drew there), then draws the glyph again through a
`painter.translate`/`scale`/`translate`-back transform centered on its
own rect, at `colorForCapture`'s color scaled by the same progress
value. This keeps every other rendering path (selection highlight,
diagnostics, the caret) completely unaware the effect exists.

Progress is eased (`t = 1 - (1-progress)²`, the same ease-out shape as
`kEaseFactor`-driven motion elsewhere) over `kTypingAnimationTicks = 4`
ticks of the existing 30ms blink-timer cadence (~120ms total) — chosen
to read as a snappy "just landed" pop rather than a sluggish delay
before newly typed text looks finished. Scale interpolates
`kTypingAnimationStartScale` (0.85) → 1.0; alpha interpolates 0 → the
capture's own resting alpha (so a typed character inside a dimmed
comment or number run still fades to *that* dimmed level, not to full
opacity).

### Aging and cleanup: `updateAnimation()` advances, `snapAnimationToTarget()` clears

`updateAnimation()` (already the one place per-frame animation state
advances) increments every entry's `elapsedTicks` and drops it once
either the window elapses or its byte range no longer fits inside
`m_cache` (a defensive bounds check — the common real hazard, an
immediate Backspace after typing, is handled directly:
`deleteBackward`/`deleteForward` aren't touched by this ADR, but the
bounds check means a shrunk buffer just silently stops rendering the
stale entry rather than reading out of range). `snapAnimationToTarget()`
— already called after undo/redo, Vim mutations, and paste to make the
caret jump instantly instead of gliding — now also clears
`m_typingAnimations` outright: everything else snaps to "already
settled" there, so a character still mid-pop when one of those fires
should too, rather than finishing its animation over text that's no
longer the reason it started.

## Consequences

Verified live: a single typed character visibly starts smaller/dimmer
than surrounding text and reaches full size/opacity within the
animation window; rapid sequential typing (`bcdef`) and an immediate
Backspace right after produced no crash or visual corruption; with
`animations = false`, a typed character appears instantly at full
size/opacity, confirming the opt-in gate actually gates it.
`ctest --test-dir build` still 9/9 (unaffected — no core-layer change).

Not covered by this ADR: Vim mode's own mutation primitives
(`vimPasteAfter`, `vimOpenLineAbove`, ...) intentionally bypass
`insertText()` entirely (ADR 0046, undo-group nesting) and so never
trigger this animation — Vim's `p`/`o`/etc. insert instantly, same as
before. Extending the pop-in there, if wanted, is separate follow-up
work, not a gap in this change.
