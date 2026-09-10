# ADR 0022: A unified floating-panel design system for editor chrome

## Status

Accepted

## Context

Phase 13 shipped `FindBar` as a bar docked full-width at the top of the
window, inside a `QVBoxLayout` wrapper `main.cpp` had to introduce
specifically for it. Direct user feedback right after: the bar "looks
so basic," and more windows of this kind are coming (Open/Save-As next,
per `docs/ROADMAP.md`'s Phase 14) — so this needed to become a real,
reusable *system* now, not another one-off widget shape per phase.

Two decisions were put to the user rather than guessed, since both
have real tradeoffs and set precedent for everything after:

1. **Where do these panels sit?** True center of the window (a
   command-palette/modal feel, consistent everywhere) vs. top-center
   (stays clear of the text being searched). Chose **true center**,
   with the explicit tradeoff noted (a centered find panel does sit on
   top of the text you're searching) — the user's call, consistent
   feel across every future panel outweighed it.
2. **Do future Open/Save-As dialogs join this system, or stay native
   `QFileDialog`?** Chose **custom floating panels** — overrides the
   native-dialog approach `~/.claude/plans/noble-herding-quokka.md`'s
   Phase 14 section had originally sketched; that plan needs updating
   before Phase 14 starts (a custom file browser is real, non-trivial
   work `QFileDialog` would have skipped entirely).

## Decision

### `FloatingPanel` (`gui/src/floating_panel.{h,cpp}`) — the reusable base

A plain **child widget** of whatever host it's given (not a top-level
`QWindow`) — simpler than real separate windows, no window-manager
compositing/platform quirks to manage, and "floats over things" is
just `raise()` plus being painted after its host in the normal Qt
child-stacking order. Owns exactly four things, nothing about content:

- **Centering**: `recenter()` sizes to `sizeHint()` (so subclasses get
  correct sizing for free from their own layout, no manual size
  bookkeeping) and centers that over the host's full `rect()`, clamped
  to a margin so a very small window never pushes it off-screen.
  Installs itself as an event filter on the host to catch
  `QEvent::Resize` and re-center live — no signal needed from the
  host.
- **Flat paint**: fills its own background with a caller-supplied
  translucent color and draws a 1px border in a caller-supplied
  low-alpha color. No shadow, no gradient, no rounded corners — "flat"
  taken literally, matching the user's explicit ask and this project's
  existing "one font color, vary only weight/opacity" pillar (ADR
  0007) applied here to chrome instead of syntax highlighting.
- **Fade open/close**: a `QGraphicsOpacityEffect` + one
  `QPropertyAnimation` (140ms, `OutCubic`) — "fast and smooth," per the
  user's ask, and short enough that it reads as instant-but-clean
  rather than a wait. `setAnimated(bool)` — checked and set fresh by
  the owning panel on every open/close, not cached at construction —
  gates it: `true` fades, `false` snaps instantly. Wired to the same
  `animations` config key the caret-fade/smooth-scroll already use
  (ADR 0012 decision 2), rather than a second motion toggle — one
  lever for "does this app move," matching that decision's own
  reasoning (a single value already does double duty for two unrelated
  animation mechanisms there; a third reuses it rather than adding a
  fourth knob).
- **No config access of its own** — every color is pushed in via
  `setColors(...)` by the subclass. Keeps `FloatingPanel` reusable for
  a future non-text panel (Open/Save-As) without any `core`/`AseConfig`
  coupling baked into the base class.

### `LetterBadge` (`gui/src/letter_badge.{h,cpp}`) — the label replacement

A small flat square with one bold letter, replacing `FindBar`'s old
"Find:"/"Replace:" text labels — "F"/"R" today, and the obvious pattern
for future panels ("O" for Open, "S" for Save-As). Same
no-config-of-its-own shape as `FloatingPanel`: colors set explicitly by
the owner. Filled with the text color (high alpha, reads as a solid
opaque chip) with the letter drawn in the background color — a
contrast-inverted micro-badge using only colors the theme already
defines, no new hue.

### `FindBar` rebuilt on `FloatingPanel`

Same public behavior as ADR 0021 (matching/highlighting/replace logic
untouched — this phase only touched chrome), restyled:
`FindBar : public FloatingPanel`, laid out with `LetterBadge`s instead
of `QLabel`s, `openFor`/`hideBar` now call `openPanel()`/`closePanel()`
instead of raw `setVisible`. `refreshTheme()` (now public, called from
`EditorViewport::checkConfigReload` too, not just on open) pulls panel/
border/badge colors from new `EditorViewport` accessors
(`panelBackgroundColor`, `panelBorderColor`, `textColor`,
`backgroundColor`, `panelFieldColor`, `animationsEnabled`) — so an
edited `config.ase` reaches this panel live, exactly like it already
does for the editor itself.

One fix beyond the ask, found during live verification: `QLineEdit`'s
default native style is a bright white field with a blue focus ring —
jarring against this app's dark palette regardless of how the panel
around it looks. Both fields get `setFrame(false)` and an explicit
`QPalette` (base = a new derived `panelFieldColor` — the background,
lightened, so a field reads as a distinct control without a new
config key; text = the theme's full-contrast text color). Not asked
for explicitly, but "flat... same colors... full contrast" doesn't
hold if the input box itself is still native-white.

### `main.cpp` simplified back

The Phase 13 `QVBoxLayout` wrapper is gone — `FindBar` is a child of
`EditorViewport` now, not a layout row, so `window.setCentralWidget
(viewport)` again, exactly as it was before Phase 13.

### New config key

`panel_background = #282828E6` — the existing `background` hue at
~90% opacity ("a small transparency that looks clean," per the ask).
`panelBorderColor()`/`panelFieldColor()` are deliberately *not* new
config keys — both are derived in code from colors that already exist,
keeping the config surface from growing one key per cosmetic detail.

## Consequences

Verified live: `Ctrl+F` and `Ctrl+H` both produce a true-centered,
flat, bordered panel with legible badges and full-contrast field text
(screenshotted); replace-one, replace-all, and undo all re-verified
working unchanged through the new container (save-to-file round trips,
same as ADR 0021). Config hot-reload path code-reviewed (calls through
identically to the editor's own background/text hot-reload) but not
independently re-screenshotted this phase.

Not built: the fade animation's actual frame-by-frame motion wasn't
captured mid-flight (140ms is faster than this project's screenshot
tooling can reliably catch a partial-opacity frame at) — verified by
code inspection only, standard `QGraphicsOpacityEffect`/
`QPropertyAnimation` usage. Phase 14 (editor chrome: status bar, dirty
tracking, Open/Save-As) needs its own plan update before it starts:
`~/.claude/plans/noble-herding-quokka.md`'s existing Phase 14 section
still describes native `QFileDialog` for Open/Save-As, superseded by
this ADR's second decision — a custom file-browsing floating panel is
real, unscoped work that plan never accounted for.
