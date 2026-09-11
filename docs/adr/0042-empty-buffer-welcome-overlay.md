# ADR 0042: Empty-buffer welcome overlay

## Status

Accepted

## Context

A fresh launch of `ase_gui` opens onto a completely blank canvas — no
indication of the app's name, and no hint that a keyboard-driven
editor with no visible menu bar has a `Ctrl+/` shortcuts reference at
all. User request: show the logo and a few essential instructions
centered over an empty buffer, fading away the moment there's
something to look at instead (typed content), and fading back in if
the buffer becomes empty again — whether or not that emptying got
saved in between.

## Decision

`EditorViewport::drawWelcomeOverlay()`, called at the very end of
`paintEvent()` in absolute widget coordinates (independent of
scroll/gutter, since it's centered on the viewport itself, not tied to
any document position): the existing "ase" wordmark
(`:/ase.png`, the same asset the About panel and app icon already use
— docs/adr/0027, docs/adr/0034) scaled to 140px wide, and four lines
of the most essential shortcuts underneath (open, save, the full
shortcuts reference, about) — not the Help panel's exhaustive list,
just enough to get someone unblocked. No redundant text title: the
wordmark already spells out the name.

### Purely content-driven, not a one-time "seen it" flag

`m_welcomeOverlayOpacity` (an eased 0..1, same animation shape as the
diagnostic focus state — docs/adr/0041) targets 1 whenever `m_cache`
(the buffer's byte mirror) is empty, and 0 otherwise, recomputed every
frame in `updateWelcomeOverlayOpacity()` — no persisted "first launch"
state anywhere. This was a deliberate simplification over a real
onboarding-flag mechanism: it satisfies every behavior actually
requested (appears on a fresh empty buffer, fades on the first
keystroke, reappears if everything gets deleted again regardless of
save state) with no new persistent state, and it doubles as a general
empty-buffer placeholder beyond just "first launch" — clearing a
file's contents to start over shows it again too, which reads as a
feature, not a gap.

Same convention as the diagnostic-focus work: called unconditionally
from `updateAnimation()`, so it still works with `animations = false`
— it just snaps instead of easing, exactly like every other animated
value in this file.

## Consequences

Verified live: screenshotted a genuinely fresh empty file's launch
(logo + instructions, correctly centered and legible at a dim-but-
readable text alpha), typed one character and caught the fade-out
mid-transition, then deleted it and caught the fade-back-in
mid-transition — confirmed reappearing even though the file was never
saved (the dirty `*` marker was still showing throughout). No new
persistent state, no new timer — reuses the existing blink-timer-
driven repaint loop every other eased value in this file already
depends on.
