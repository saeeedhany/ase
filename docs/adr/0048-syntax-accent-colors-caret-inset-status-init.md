# ADR 0048: Syntax accent colors, caret inset, smaller default font, immediate status label

## Status

Accepted

## Context

Direct user feedback after using the Phase 1 Vim mode and its new block
cursor (ADR 0046, ADR 0047): drop italics from syntax highlighting and
add real color instead, using a specific color the user had already
supplied once before (`#689d6a`, ADR 0036's docs-site accent) plus one
more color that pairs with it, kept restrained ("don't use too much");
make both caret shapes a little shorter; make the default font a
little smaller; show the Vim mode label in the status bar immediately
on launch, not just after the first keystroke; and (tracked separately,
see the "not in this ADR" note below) add a typing animation.

## Decision

### Two real accent colors, replacing italic entirely

ADR 0007's "one font color" pillar meant every syntax capture rendered
in `m_textColor`'s own hue, varying only weight/opacity — `ASE_HL_TYPE`
was the one exception, rendered italic. Italic is gone now; in its
place, `ASE_HL_TYPE` and `ASE_HL_STRING` are the two captures that get
a real, distinct color:

- `syntax_type = #689d6a` — the color the user supplied previously for
  the docs site (ADR 0036), reused here since it's already an
  established part of this project's identity, not a new arbitrary
  choice.
- `syntax_string = #d79921` — a warm, muted gold chosen to pair with
  the given aqua-green without competing with it or with the editor's
  own warm cream text color; both land in the same desaturated,
  low-contrast register as everything else in this theme rather than
  reading as a bright, saturated "rainbow" addition.

`ASE_HL_KEYWORD` (bold), `ASE_HL_NUMBER`/`ASE_HL_COMMENT` (dimmed
alpha), and `ASE_HL_NONE` (plain) are unchanged — still monochrome,
varying only weight/opacity. Only two of the five captures gained a
hue, deliberately, per "don't use too much": this is a small, scoped
departure from ADR 0007's pillar, not its replacement. Both colors are
configurable (`syntax_type`/`syntax_string` in `config.ase`, defaults
baked into both `ase_config_create_default()` and
`kDefaultConfigTemplate` in `core/src/config.c`), consistent with every
other themeable color in this editor.

Removing italic also let `m_italicMetrics`/the italic `QFont` setup in
`applyConfig()` be deleted outright rather than kept dead — nothing
else in this codebase ever used italic.

### Both caret shapes inset a little vertically

`kCaretVerticalInset = 2` (px) trims 2px off the top and bottom of both
the bar caret (Insert/Visual) and the Normal-mode block cursor's fill
rect (ADR 0047) — a full-line-height caret read as slightly too
tall/blocky against the actual glyph height. The block cursor's
knocked-out glyph (drawn on top, in the background color) still uses
the *un-inset* rect, so the character's own vertical centering/baseline
stays identical to normal text; only the surrounding fill shrinks.

### Default `font_size`: 12 → 11

A one-point reduction, in `ase_config_create_default()`,
`kDefaultConfigTemplate`, and the `ase_config_get_int` fallback literal
in `applyConfig()` — all three needed updating to actually stay in
sync (`core/tests/test_config.c`'s default-value assertions updated to
match). This only affects a *newly generated* config file
(`ase_config_write_default_if_missing()` never touches an existing
one) — an already-existing `~/.config/ase/config.ase` keeps whatever
`font_size` it already has and needs a manual edit to pick up the new
default.

### Status bar shows the mode label immediately, not just after the first keystroke

`main.cpp`'s status `QLabel` was constructed with a hardcoded literal
(`"Ln 1, Col 1"`) and only replaced once `EditorViewport::statusChanged`
first fired — which happens at `ensureCursorVisible()`, called from
every cursor-moving action and edit, but not at construction. A
Vim-mode user launching the editor therefore saw a bare, mode-less
status line until their very first keystroke. Fixed with a new public
`EditorViewport::emitInitialStatus()` (a one-line wrapper around the
already-private `ensureCursorVisible()`, added instead of just making
that private method public, since nothing else outside the class
needs it), called once in `main()` right after `window.resize()` and
before `window.show()` — so the label already reads e.g. `"INSERT  Ln
1, Col 1"` the instant the window appears.

### Not in this ADR: the requested typing animation

A fourth request — "add an animation while typing to give a good
effect" — is deliberately not designed or built here. Unlike the three
decisions above, it has no single obvious shape (per-character
fade/scale-in vs. a caret pulse vs. something else entirely), each
with real, different implementation cost and feel; picked without
asking, it's the one item in this batch most likely to need throwing
away and redoing. Left for a follow-up ADR once the direction is
confirmed.

## Consequences

Verified live (screenshots, real GUI on a C test file exercising every
capture kind): keywords/`typedef`/`const`/`struct` bold and plain-color
as before; every `int`/`void`/`double`/`char`/`Point` type renders in
the aqua-green, no italic anywhere; string literals (`"hello, world"`,
a format string, `<stdio.h>`) render in the new gold; comments and
numbers keep their existing dimmed-alpha treatment. Both caret shapes
visibly shorter than the full line height on inspection, with the
block cursor's knocked-out glyph still correctly centered. The status
bar reads `"INSERT  Ln 1, Col 1"` on the very first frame after launch,
before any input. `ctest --test-dir build` still 9/9 (after updating
`test_config.c`'s two hardcoded `12`s to `11`).
