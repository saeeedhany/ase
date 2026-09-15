# ADR 0090: Scrolling runs at frame rate

## Status

Accepted

## Context

Scrolling was reported as the least satisfying thing about the editor —
"good, but I feel it would be more". Not a stutter, not a hitch: a
ceiling.

The measurement said exactly that. Frame intervals during a scroll,
logged from `paintEvent`, sat at a median of **30.3 ms — 33 fps** — with
paint itself costing only **1.3 ms** on a 277 KB file. Twenty-eight of
every thirty milliseconds were spent waiting. The renderer was never the
limit; the clock was.

`motion::kTickMs = 30` is that clock, described in its own comment as
"the heartbeat every frame-driven value steps on". The scroll glide
advances one eased step per paint, and paints come from that timer, so
the glide could not exceed 33 fps however fast the machine was.

This was not a mistake so much as an untested assumption: 30 ms is a
reasonable-looking number, and nothing in the codebase had ever measured
what it cost.

## Decision

### The tick is 16 ms

60 fps, and scrolling measures at **62 fps** against the same battery of
scroll events. Paint at 1.3 ms leaves ~92% of a 16 ms frame idle, so
there is headroom for 120 Hz later; 16 ms is the safe standard rather
than the limit.

### Durations are derived from milliseconds, not counted in ticks

Four animations counted in ticks — typing pop-in at 4, the caret breathe
at 24, the hard blink at 12, the line edit's copies of both. Every one of
them would have silently doubled in speed when the tick halved, which is
the trap that makes a constant like this frightening to touch.

So `motion::ticksFor(ms)` now derives them:

| | was | now | duration |
|---|---|---|---|
| typing pop-in | 4 | `ticksFor(120)` = 7 | ~120 ms |
| caret breathe | 24 | `ticksFor(720)` = 45 | ~720 ms |
| hard blink | 12 | `ticksFor(360)` = 22 | ~352 ms |

The numbers on the right change; what the user sees does not. And the
next person to change `kTickMs` re-rates the loop without re-timing
every animation in it.

### An idle tick does not repaint

Doubling the tick doubled the idle cost first: with animations on, the
timer called `update()` every tick forever, purely to advance the caret's
breathe. Measured, that took idle CPU from ~6% of a core to **12.2%** —
paying 60 fps of full-viewport repaints to animate a 720 ms fade.

The timer now repaints every tick only while something is moving —
scroll still easing, a typing animation live, or within 400 ms of the
last interaction (`resetCaretBlink()` zeroes `m_idleTicks`, so that
recency is already tracked) — and every other tick otherwise. Idle CPU
came back to **6.0%**, unchanged from before this ADR, with scrolling
still at 62 fps.

The breathe renders at ~31 fps while nothing else happens. On a 720 ms
fade that is not a thing anyone can see, which is the whole argument for
spending frames where motion actually is.

## Consequences

Scrolling is twice as smooth for no idle cost. The gain was available
the entire time and cost one constant, which is worth saying plainly:
the expensive part was not the fix but finding out that 33 fps was a
number nobody had chosen on purpose.

Idle CPU is *unchanged*, not improved. 6% of a core to breathe a caret
is still more than it should be — the repaint is full-viewport when only
the caret rect changed. Fixing that means teaching `paintEvent` to serve
a caret-only region, which is a real change and a separate one. Turning
`animations` off avoids it entirely today.

`ticksFor()` truncates, so a duration shorter than one tick clamps to a
single tick rather than zero. Nothing currently asks for less than 16 ms.
