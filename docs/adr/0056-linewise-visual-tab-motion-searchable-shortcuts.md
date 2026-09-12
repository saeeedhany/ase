# ADR 0056: Linewise Visual, tab motion, and a searchable shortcut reference

## Status

Accepted

## Context

A round of feedback from real use, spanning Vim fidelity, the tab strip's
motion, the file panel's legibility, and the shortcut reference.

## Decision

### `Shift+V` linewise Visual, and `o` to swap ends

Two genuine gaps against real vim, both from ADR 0046's Phase 1 scope.

`V` is a flag on `VimMode::Visual` rather than a fourth mode. Every
operator, motion and render path already works off the selection range,
so linewise only has to keep that range snapped to whole lines —
`vimNormalizeLinewiseSelection()` pushes anchor and cursor out to the
outer edges of the two lines involved, and nothing downstream needs to
know linewise exists.

The subtlety worth recording, because it was a real bug caught by
testing rather than by reading: normalizing parks the cursor at the
**start of the following line**, because that is what makes the selection
include the trailing newline and therefore makes a linewise delete remove
whole lines instead of leaving blanks behind. But that means
re-deriving the cursor's line from its offset reads one line too far —
and the error compounds on every motion, so `V` then `j` selected three
lines instead of two. Fixed by tracking `m_vimVisualCursorLine`
explicitly and restoring the real cursor onto it before each motion
(`vimPrepareLinewiseMotion()`).

`o` swaps cursor and anchor, so you can fix the end you didn't mean to
extend without reselecting. In linewise mode it swaps the tracked lines
too. A linewise `y` now records the yank as linewise, so `p`/`P` paste it
as whole lines.

The status bar distinguishes `VISUAL` from `VISUAL LINE`, as vim does.

### The tab strip: fixed slots, and everything interpolated

The strip felt jumpy for a concrete reason: **only the active tab
reserved width for its close mark**, so every switch changed every tab's
width and the whole strip reflowed. Now every tab reserves that space
whether or not the mark is drawn, and the mark fades in on the active
one. Switching moves nothing. (Verified: the leftmost ink sits at the
same x before and after a switch.)

On top of that, all layout change is animated through one
`QVariantAnimation` on `motion::kChrome`/`motion::kCurve` (the shared
language from ADR 0053). Each tab interpolates from where it *currently
is* toward where it now belongs, which means:

- A new tab has no previous position, so it starts at the **position of
  the tab it was opened from** and fades up as it slides out to its own
  slot — the requested "it comes from the other one" effect.
- Active/inactive opacity crossfades rather than snapping.
- Interrupting a slide resumes from the visible position instead of
  restarting, because `relayout()` samples the current interpolated
  position as the new starting point.

Also: the strip now uses the **editor's own font** and more generous
padding, so tabs sit at the same visual weight as the text they label
(and follow `Ctrl+=`/`Ctrl+-` with it) rather than reading as a thin
afterthought. A `+` at the right edge opens a new tab; it is pinned to
the edge so it doesn't move as tabs come and go.

One crash fixed on the way: `std::clamp(previousActiveIndex, 0, size-1)`
asserts when the list is empty (`hi < lo`) — the very first tab has
nothing to emerge from, and the empty check that was meant to guard it
ran one line too late.

### File panel legibility

Two things that were wrong rather than merely plain:

- **The placeholder was effectively invisible.** `QPalette::PlaceholderText`
  was never set, and Qt's derivation from `Text` lands almost black
  against this theme's dark field. Set explicitly, one tier down.
- **The row highlight reused the editor's selection colour**, which is
  tuned for text on the editor background; over the panel's lighter
  field it came out muddy rather than lit. It is now derived from the
  text colour at low alpha, with a small corner radius
  (`TranslucentBar::setRadius`) so a bar sliding between rows reads as a
  highlight rather than a block.

### Shortcuts: collapsible sections and search

Shortcuts are the primary way this editor is driven, so the reference is
something you come back to and scan — not read once. One long rich-text
blob made you hunt.

It is now a list of section widgets: a clickable header (caret + title +
the dim "what enables this" note) and a rows table. Clicking a header
folds it. A search field filters across keys, descriptions and section
titles, hides sections with no surviving rows, and force-expands what
still matches — a collapsed section is a browsing preference and should
not hide results. Escape clears an active search before closing the
panel, since losing your filter is a smaller surprise than losing the
panel.

A bug found while building it, worth recording because the symptom
pointed the wrong way: the text rendered in colours that looked wrong,
and the obvious suspect was the `QString::arg` colour substitution.
Dumping the generated HTML showed it was **perfectly correct**. The real
cause was that the scroll area's new *container widget* was never
themed, so everything painted on Qt's default near-white — against
which the cream description column is nearly invisible. The lesson:
when colours look wrong, check what is behind them before suspecting
the colours.

## Consequences

Verified live: `V` then `j` selects exactly two whole lines (it selected
three before the cursor-line fix); `o` then `k` extends the opposite end;
a new tab is caught mid-slide emerging from the tab it was opened from;
switching tabs leaves the strip's left edge at an identical x; the
shortcut panel filters on "vim" to just the matching sections, and a
clicked header collapses to `▸`. `ctest` 9/9, clean build, zero warnings.

Unchanged and still open: the tab strip does not scroll or elide, so
past roughly a dozen tabs the later ones run off the window edge.
