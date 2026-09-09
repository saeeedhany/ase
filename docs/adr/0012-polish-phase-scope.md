# ADR 0012: Phase 7 polish — scope and an accessibility fix

## Status

Accepted

## Context

Spec section 7, phase 7: "Multi-cursor, minimal/opt-in animations,
panel layout, accessibility pass." Four distinct items, none deeply
specified — this ADR records what each one became and why.

## Decisions

### 1. Multi-cursor: keyboard (`Ctrl+D`) and mouse (click / `Alt`+click),
byte-offset model, no selection ranges yet

`EditorViewport` moves from a single `size_t m_cursor` to
`QVector<size_t> m_cursors` (always non-empty, always sorted ascending,
de-duplicated after every edit or movement). Every per-cursor primitive
(`insertTextAt`, `deleteBackwardAt`, `moveCursorLeftAt`, etc.) takes the
cursor to act on as a parameter; multi-cursor operations are a loop over
`m_cursors`, processed **highest offset to lowest**. That ordering is
what makes this safe without cross-cursor offset bookkeeping: every
edit's affected byte range starts at-or-after the acting cursor's own
(pre-edit) position, and every other cursor still to be processed is,
by sort order, strictly before that position — so it's never inside or
past the shifted region. No cursor ever needs to know about any other
cursor's edit. (This also holds for the UTF-8-continuation-byte-aware
multi-byte backspace/delete from ADR 0006 — the affected range's late
boundary is still exactly the acting cursor's position, so the
invariant carries through unchanged.)

New cursors come from:
- **Mouse click** (new — `mousePressEvent` didn't exist before this
  phase): plain click collapses to a single cursor at the clicked
  position; `Alt`+click adds one there instead.
- **`Ctrl+D`**: finds the word at the last (highest-offset) cursor,
  searches forward for the next whole-word match, and adds a cursor at
  its end — the well-known "select next occurrence" gesture from
  Sublime/VS Code, without needing this editor's largely-absent
  selection-range model (v1 has point cursors, not ranges — see ADR
  0006). Doesn't wrap around the buffer; if nothing matches forward
  from there, it's a no-op.
- **`Escape`**: collapses back to one cursor (the last one), the
  standard exit hatch out of multi-cursor mode.

Vertical (up/down) movement's sticky-column behavior (ADR from Phase 2)
is kept only in single-cursor mode; with multiple cursors active, each
recomputes its column fresh every time rather than tracking one shared
sticky column per cursor. Simpler, and the edge case where it visibly
differs (holding up/down through varying line lengths with several
cursors active) is a minor, rare-in-practice paper cut, not a
correctness issue.

### 2. Animation: one, opt-in, off by default — smooth caret fade

Rather than the hard on/off blink from Phase 2, an `animations = true`
config key (default **false**, both because "opt-in" and because
motion is something some users specifically need off — this default
serves both the aesthetic pillar and accessibility at once) switches
the caret to a sinusoidal alpha fade instead of a hard toggle. Chosen
over animated smooth-scrolling — the other obvious candidate — because
it's genuinely small: it only changes how one rect is painted, and
touches nothing about scroll-range or line-visibility calculations that
a smooth-scroll implementation would need to get right (partial lines
at the viewport edges, an animated fractional scroll position feeding
into `ensureCursorVisible`, etc.). "Minimal" is doing real work in
"minimal/opt-in animations" — this is the version of that phrase that
doesn't grow into a rendering-pipeline change.

### 3. Panel layout: nothing to lay out yet — deliberately not invented

This editor has exactly one panel: the text viewport. No file tree, no
diagnostics panel, no status bar exists in any phase's scope so far
(spec section 3's architecture diagram lists "file tree / project
explorer" as a *possible* future feature module, but no phase — 1
through 7 — ever schedules building one). Building a second panel now,
purely to have something to "lay out," would be exactly the kind of
unscoped feature addition the "Minimal" pillar exists to prevent — and
would sit awkwardly against "no visible chrome by default" besides.

Deferred, not skipped: panel layout is real work for whenever a second
panel actually exists — most plausibly a diagnostics panel once the
Phase 6 LSP client gets a GUI (already a tracked `docs/ROADMAP.md`
follow-up), or a file tree if that's ever prioritized. Tracked as a
follow-up rather than closed out here.

### 4. Accessibility pass: real, verified wins; one honestly-flagged gap

**Fixed — comment dimming failed WCAG AA.** ADR 0007's default theme
renders comments at 45% opacity of the text color (alpha 115/255) for
de-emphasis. Computing the actual contrast ratio (this project's own
palette, not a generic check) against the `#282828` background: **3.64:1**
— fails WCAG AA's 4.5:1 threshold for normal-size text (it clears the
3:1 "large text/UI component" bar, but comments render at body-text
size). Raised to alpha 145/255 (~57%), which measures **4.91:1** — a
real margin above 4.5:1, not a knife's-edge pass — while still reading
as visibly dimmed relative to full-opacity text. `keyword`/`type`
(rendered via weight/style, not opacity — full-opacity text, ADR 0007)
and `string`/`number` (alpha 200/255, **7.94:1**) were already
comfortably compliant; only comments needed the fix. Full-opacity text
against the background itself measures **11.96:1** — WCAG AAA.

**Added:** `EditorViewport::setAccessibleName`/`setAccessibleDescription`
— cheap, safe, gives assistive technology *something* to identify the
widget by, even short of full text exposure.

**Verified, not new:** the editor was already fully keyboard-operable
(every Phase 2 interaction — navigation, editing, save, quit — has a
keyboard path; there's no mouse-only affordance) before this phase
added mouse support alongside it, so this phase doesn't regress that.

**Deliberately not attempted: full screen-reader text exposure.**
Making a custom-painted `QWidget` legible to a screen reader means
implementing `QAccessibleInterface`/`QAccessibleTextInterface`
correctly — a real, sizeable surface (a dozen-plus pure virtual
methods spanning geometry, roles, states, and text/caret/selection
queries) that a real assistive-technology client will actually probe.
This project has no way to verify that against a live AT-SPI client in
this environment. Shipping an unverified implementation of an interface
real AT tooling will call into risks a worse outcome than not having
one — the same reasoning as ADR 0011's decision not to ship untested
Windows async I/O. Tracked as a real, open gap in `docs/ROADMAP.md`,
not silently dropped.

## Consequences

Multi-cursor is the substantial addition here; the other three items
are deliberately much smaller in proportion to what they could have
been, each for a stated reason rather than by omission. The comment-
contrast fix is the one item in this ADR that's a correction to a
previous phase's decision, not new work — worth tracking because it's
the kind of thing that's easy to ship once and never revisit.
