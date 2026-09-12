# ADR 0057: An always-present tab strip, and a welcome screen worth reading

## Status

Accepted

## Context

More feedback from use, all about the tab strip and the first thing you
see when the editor opens.

## Decision

### The strip is always there

ADR 0054 hid it below two tabs, reasoning that a single tab is chrome
telling you nothing the title bar doesn't. In practice that is worse:
the strip appears and disappears underneath you as you open and close
files, so the thing you look at to know what you have open is the one
thing that moves. And a pathless buffer had nothing anywhere calling it
`untitled` — every other editor shows exactly that from the moment it
starts.

So the strip is present whenever there is a buffer, which is always.
This supersedes ADR 0054's collapse-below-two rule.

### Switching a tab is not an animation

The real complaint — "when I click on one of them they all give the same
effect each time" — was a bug, not a preference. `setEntries()` animated
on *every* call, so merely changing which tab was active replayed the
whole interpolation: the close mark faded in from zero, opacities
crossfaded, and clicking around felt like the app stuttering rather than
responding.

Animation is now gated on the tab *set* actually changing. Opening and
closing move things; switching is a state change and lands instantly.
Verified rather than assumed: a screenshot taken 25ms after a click is
**pixel-identical** to the settled one.

### Closing reverses opening

A closed tab does not vanish. It stays drawn as a ghost — marked
`closing`, excluded from hit-testing — and collapses toward whichever
tab now holds focus while fading out, the exact reverse of arriving out
of the tab it was opened from. Ghosts are dropped when the animation
finishes.

### Overflow pans with Shift+wheel

Past a certain count the tabs run off the edge. `Shift`+wheel pans the
strip, matching the convention browsers and terminals already use for
horizontal scroll rather than inventing a key; a plain wheel is left
alone so it still reaches the editor underneath. The `+` keeps a
reserved strip at the right edge that tabs are never allowed to slide
under, and is painted last over its own background so a panned tab
cannot appear through it.

Note for anyone testing this: `xdotool click --window <id> 5` does *not*
reach Qt as a wheel event. Real pointer events (`xdotool mousemove` to
absolute coordinates, then `click 5` with no `--window`) are required.
The first attempt appeared to show scrolling as broken when it was the
test that was wrong.

### The welcome screen says what the editor is, then what to press

Restructured to: logo, version, byline, then **"The absolutely simple
text editor"** as the strongest line — all centered — followed by the
shortcut list in its existing two-column, dot-led alignment.

The shortcut list was re-chosen rather than kept. It had gone stale
against a much larger app, and more importantly it listed things you
could guess while omitting the one thing that will actually stop a new
user dead: **Vim mode is on by default now (ADR 0050), so the editor
opens in Normal mode and typing does nothing until you press `i`.** That
is now the second line. `Ctrl+/` is first because it is the answer to
every other question, followed by new file, open, save, and the command
line.

Three opacity tiers carry the hierarchy — tagline strongest, shortcuts
mid, version and byline dimmest — rather than any second colour or rule
(ADR 0007).

## Consequences

Verified live: a bare `ase_gui` shows one `untitled` tab plus the `+`,
with the new welcome layout beneath it; eight open tabs overflow and
`Shift`+wheel pans them (the leftmost clips, the rightmost is revealed);
a click on an existing tab is pixel-identical at 25ms and at rest; a
closed tab is still drawn 50ms after `Ctrl+W`, gone by 550ms, with its
neighbours shifted into place. `ctest` 9/9, clean build, zero warnings.

Not isolated frame-by-frame: the ghost's fade *curve*. That it renders
and then disappears while neighbours reflow is confirmed; the
interpolation itself is the same code path already proven for the open
animation, so it was not worth further sub-frame capture work.

## Addendum: what was actually wrong with the tab animation

Two rounds of fixes here missed the real cause, so both the wrong
diagnoses and the right one are recorded — the wrong ones were plausible
and someone will reach for them again.

**The actual bug: tabs were identified by display name.** `relayout()`
matched a tab across layouts with `previousNames.indexOf(tab.name)`, and
names are not unique. Every buffer opened with `+` is called `untitled`,
so the collision was immediate and constant rather than a rare edge
case (two files sharing a basename in different directories would do it
too).

That single mistake produced every reported symptom:

- A new `untitled` matched an *older* `untitled`, so it was treated as an
  existing tab: no fade-in, and it slid from whatever position that older
  tab held — which is why new tabs appeared to come from an early tab no
  matter how many were open.
- On close, the ghost check asked "is a tab with this name still open?"
  With duplicates the answer was always yes, so **no ghost was created
  and the close animation was skipped entirely.**
- Survivors matched the wrong entries and inherited the wrong positions
  and opacities, which is the "random behaviour" on delete.

Named files masked all of it, which is why every earlier verification
pass — all of which used distinct `.c` files — showed it working.

Tabs are now keyed on a `quintptr id`: the `EditorViewport` pointer,
which is unique and stable for as long as the buffer is open. The
display name is only ever drawn, never used for identity.

**The two earlier fixes were real, but secondary.** Both are kept:

- *Origin.* New tabs animated from "whichever tab was active", but new
  tabs are always appended, so an active tab further left meant a long
  sweep across the strip. The origin is now the tab immediately to the
  new one's left.
- *Survivor opacity.* Survivors started from the previous frame's alpha
  and interpolated to their settled value; with the old 0..1 "how active"
  encoding an inactive survivor started from a value that did not match
  where it already was, so it visibly re-faded. Survivors now start at
  the alpha they already have, and `Tab::fromAlpha` is a real 0–255 alpha
  — the old encoding also could not express "start fully invisible", so
  new tabs faded in from the inactive tier instead of from nothing.

Verified with five buffers all named `untitled`: the fifth slides in from
the fourth; closing one leaves the strip's ink extent at an intermediate
value 18ms in (671 → 562 → 534 px), proving a ghost is drawn and
collapsing where previously there was none.

The lesson worth keeping: **display strings are not identity.** The bug
was invisible to every test that used distinct filenames, and only
reproduced through the one entry point that generates duplicates.

