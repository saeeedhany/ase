# ADR 0053: Fixing the render hot path, and one motion language

## Status

Accepted

## Context

Two cleanup asks ahead of stabilising: make the editor actually live up
to "blazingly fast" in every state, and make animation properties global
so new chrome inherits the established feel instead of re-deciding it.

Both turned out to have a concrete, measurable problem underneath rather
than a stylistic one.

### The editor burned 22% CPU doing nothing

Measured, not assumed: an 8402-line C file open, no input, window idle.

| file | idle CPU |
|---|---|
| 5 lines | 0.0% |
| 8402 lines | **22.0%** |

Scaling with file size pointed straight at the render path.
`capturesForLine()` allocated a per-byte vector and then scanned **every
highlight span in the entire file** to fill it — and it is called once
per visible line inside `drawLine`, again inside `xForColumn` (itself
called for caret positioning, selection rects, and find-match rects).
With `animations = true` the blink timer calls `update()` every 30ms
forever, so that whole O(visible_lines × spans_in_file) sweep ran ~33
times a second, permanently, whether or not anything had changed.

### Motion was consistent by luck, not by construction

The same decisions were being re-made independently in six files:

| file | duration |
|---|---|
| `file_browser_panel.cpp` | 85ms row highlight |
| `tracking_popup.cpp` | 90ms fade, 110ms move |
| `floating_panel.cpp` | 110ms open/close |
| `smooth_scroll.cpp` | 140ms |
| `editor_viewport_render.cpp` | `kEaseFactor` 0.68, 24-tick caret cycle |

They did all agree on `QEasingCurve::OutCubic` — the part carrying most
of the perceived feel — but only because each author looked at what the
previous one did. Nothing made that inheritance automatic, and nothing
stopped the next widget from quietly picking a seventh number.

## Decision

### Flatten the highlight spans once per edit, not per line per frame

`refreshCache()` now also builds `m_captureAt`: one capture byte per
buffer byte, filled by walking the spans once. `capturesForLine()`
became a straight slice of it.

This is the same trade `m_cache` and `m_lineStarts` already make
(docs/adr/0006): mirror the buffer once after an edit, then index it
cheaply for the rest of the frame's work. `uint8_t` rather than the enum
keeps it at 1 byte per buffer byte (~250KB for the 8402-line test file),
next to a buffer mirror that already costs more than that.

Result, same file, same idle conditions: **22.0% → 0.0%**. Syntax
highlighting output verified pixel-identical.

### What was deliberately *not* optimised

`recomputeMatches()` is O(buffer) with a full `toLower()` copy, but it
early-returns when no find query is active, so it never runs on a normal
keystroke. Its "cheap enough at this project's scale" comment is honest;
left alone.

The remaining per-keystroke cost — measured at **~12.7ms of CPU per
character** on the 8402-line file — is dominated by Tree-sitter
re-parsing the whole file, which docs/adr/0007 (decision 5) chose
deliberately and explicitly. Making that incremental is a real feature
with its own design (`ts_tree_edit` + reusing the previous tree), not a
cleanup; it is now a named roadmap item rather than something smuggled
into this pass.

### One motion language: `gui/src/motion.h`

A single header declaring the curve, the duration tiers, the pop scale,
the frame-driven ease factor, and the animation tick. Every one of the
six sites above now pulls from it, via `motion::apply(animation, tier)`
for the Qt-animation cases.

Tiers are named by **role**, not by number, because the point is that a
new widget author asks "what is this?" and gets the answer, rather than
picking a duration:

- `kQuick` (85ms) — small local change inside a visible surface
- `kFade` (90ms) — appear/disappear in place, no travel
- `kChrome` (110ms) — chrome that moves or opens
- `kScroll` (140ms) — viewport-scale travel

**Every value is exactly the one already tuned and accepted** across ADRs
0022/0024/0027/0043. This centralises them; it deliberately does not
retune them. Flattening five durations into one number would have
changed accepted behaviour, and "they're all roughly 100ms" is not a good
enough reason to overwrite deliberate per-surface tuning.

The header also documents the one genuinely non-obvious thing: the
viewport uses a *different mathematical model* (per-frame exponential
decay on remaining distance) from the tiers (fixed-duration curve),
because caret and scroll must stay responsive to input arriving
mid-flight — a fixed-duration animation would have to be cancelled and
restarted on every keystroke. Both are tuned to land in the same
perceptual range (~120ms to cover ~95% of the distance), which is why
they read as one app despite the different maths. That relationship was
previously written down nowhere.

The rule the header exists to enforce, stated in it: **new animated
chrome picks an existing tier; it does not invent a duration.**

## Consequences

- Idle CPU on a large file goes from 22% to 0%, and no longer scales with
  file size. The editor's own description ("blazingly fast") is now true
  in the state most likely to be tested — a real file, sitting open.
- Animation properties are inherited by construction: a new panel calls
  `motion::apply(anim, motion::kChrome)` and is automatically consistent
  with everything else, including if the language is later retuned in one
  place.
- `motion.h` is now a small, deliberate choke point. That is the intent —
  a diff that adds a seventh duration is now visible as such in review,
  where before it was invisible.
- Cost: one more byte per buffer byte held in memory, and `motion.h` is a
  header every animated widget includes, so touching it rebuilds them all.
  Both are cheap for what they buy.

Verified: clean build, zero warnings; `ctest` 9/9; syntax highlighting
pixel-identical on the large file; floating-panel scale+fade, find,
Vim mode, typing pop-in and scroll all still behave as before.
