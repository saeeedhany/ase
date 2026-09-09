# ADR 0016: Fix invisible caret; blink resets on activity, not on a fixed cycle

## Status

Accepted

## Context

User-reported regression, immediately after Phase 9 (ADR 0015) landed:
the caret was completely invisible with the default configuration.

Root cause: `updateAnimation()`'s `!m_animationsEnabled` branch called
`m_renderedCaretPos.clear()` every frame. `paintEvent` only draws
carets when that array is non-empty. Since `animations = false` is the
shipped default, the caret never rendered at all outside the opt-in
animated path — a real, severe regression, not a cosmetic one.

Alongside the fix, a related request: the hard (non-animated) blink
has two discrete states, and it was possible for the toggle to land at
an inconvenient moment relative to a keystroke, making "force visible
on input" feel unreliable. Wanted: solid-visible for the whole
duration of active use, a normal resumed blink only once input stops.

## Decisions

### 1. The disabled branch snaps to targets, it doesn't clear

Fixed to match every other "animations off" path in this file: compute
and store the exact current target for every cursor, same as the
steady-state result of the animated path once it's converged. Never
leaves the array in a state `paintEvent`'s `!isEmpty()` check would
skip.

### 2. Two independent tick counters, not one shared one

`m_caretTick` keeps driving the animated fade phase, untouched.
A new `m_idleTicks` — ticks since the last cursor-moving action, reset
by a new `resetCaretBlink()` called from both `keyPressEvent` and
`mousePressEvent` — drives the hard blink's toggle instead. Splitting
these means resetting on activity can't also restart the fade's
sine phase (which would otherwise happen if both used the same
counter): each mode's timing stays exactly as tuned, independently.

`resetCaretBlink()` sets `m_caretVisible = true` and `m_idleTicks = 0`.
The blink timer's existing pre-increment-then-modulo check
(`m_idleTicks % 17 == 0`, ~500ms at the 30ms tick) means resetting to 0
does *not* immediately re-toggle on the very next tick (it becomes 1,
not a multiple of 17) — so a keystroke can never be immediately undone
by a coincidentally-timed blink. As long as input keeps arriving faster
than ~500ms apart, the toggle never fires at all; the first toggle
after activity stops always lands ~500ms later, the same interval the
blink already used — "resume blinking normally" falls out for free
rather than needing a separate grace period.

## Consequences

Verified live: caret visible immediately on launch with the default
config (was the reported bug); stays solid across a rapid sequence of
keystrokes (200ms apart, all under the 500ms threshold); resumes
toggling roughly 500ms after the last keystroke, sampled at ~600ms
(invisible) and ~1150ms (visible again) of idle time. Full suite still
green — this bug was in rendering-only state, untouched by any test.
