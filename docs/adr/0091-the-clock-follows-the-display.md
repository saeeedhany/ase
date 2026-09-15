# ADR 0091: The animation clock follows the display

## Status

Accepted

## Context

[ADR 0090](0090-scrolling-runs-at-frame-rate.md) took the animation tick
from 30 ms to 16 ms and scrolling from 33 fps to 62 fps. It replaced one
hardcoded number with a better hardcoded number, and the very first
question asked of it was the right one: *why 60, when the screen does
144?*

There was no answer. 16 ms was chosen because 60 Hz is the common case,
which is the same reasoning that produced 30 ms — a plausible constant
nobody had checked against the hardware.

## Decision

### The clock is read from the screen

`motion::tickMs()` is now a runtime value derived from
`QScreen::refreshRate()`: 144 Hz gives a 7 ms tick, 60 Hz gives 17 ms,
240 Hz gives 4 ms. The ceiling is whatever the panel can actually show,
which is the only defensible number and the one nobody has to pick.

Bounds exist because the reported rate cannot be trusted blindly. Below
24 Hz or above 480 Hz is a misreport, not a display, and the current
clock is kept instead. The tick itself clamps to 4–33 ms: past ~240 Hz
the timer's own resolution dominates and more repaints stop buying
smoothness.

Headless platform plugins report 60 Hz, so the test harness and CI
behave exactly as they did before.

### Tick counts became runtime, because they had to

[ADR 0090](0090-scrolling-runs-at-frame-rate.md) made the animation
durations derive from `ticksFor(ms)` rather than be written as tick
literals, and that was what made this change small: with a compile-time
tick, every animation's duration was baked in at build time and a
runtime rate would have been impossible without revisiting all of them.

They are now inline functions rather than `constexpr` values. Same
durations, resolved per call.

### The idle repaint is timed, not counted

ADR 0090 repainted "every other tick" when nothing was moving. At 144 Hz
that would be 72 fps of full-viewport repaints to breathe a caret —
the idle cost it had just finished removing, back again and worse.

It is now every `ticksFor(32)` ticks: ~31 fps of idle repaint at any
refresh rate. The rule is that anything expressed in ticks must be
expressed in milliseconds instead, or it silently re-rates with the
clock.

### `max_fps` caps it

A config key, unset by default. It lowers the clock and never raises it
— the display's rate stays the ceiling — for anyone who would rather
have the battery than the frames. Verified at 30 and 15, which measured
31 fps and 16 fps.

### The screen is polled, not signalled

`QWindow::screenChanged` is the obvious wiring and does not work here:
the viewport is constructed before its window has a native handle, so
`windowHandle()` is null and the connection is silently never made. The
rate is re-read on the 750 ms config poll instead, which already exists
and is already asking whether something outside the process changed.
Dragging a window to another display re-rates it within 750 ms.

## Consequences

Two uninitialised members — `m_blinkTimer` and `m_configTimer` — were
found by this change crashing on startup, because `applyConfig()` runs
from the constructor and read the timer pointer before the constructor
had assigned it. They are `= nullptr` now. The crash was mine and
immediate; the landmine was older, and any future code touching a timer
from `applyConfig()` would have found it the same way.

Higher refresh rates cost proportionally more CPU while something is
moving — a 144 Hz scroll does 2.4× the repaints of a 60 Hz one. That is
the point, it is bounded by how long a scroll lasts, and `max_fps` is
there for anyone who disagrees.

The full-viewport repaint for a caret-sized change is still unfixed
([ADR 0090](0090-scrolling-runs-at-frame-rate.md)), and its idle cost is
now rate-independent rather than reduced.
