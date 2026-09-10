# ADR 0028: Click precision fix, deletion glide, select-all, About centering, synced input blink

## Status

Accepted

## Context

Direct user feedback right after ADR 0027's visual polish round:

1. Enter now glides (ADR 0027) — "but what about deletion?"
2. The About panel's logo is centered; the text below it wasn't,
   reading as misaligned.
3. A real, reported bug: clicking to place the cursor "is not
   accurate... I must move the pointer a little bit to start to move
   it the side I want."
4. `Ctrl+A` select-all didn't exist.
5. Wants the caret animation to be "global," including inside the
   floating panels' input fields.

## Decision

### Deletion glides too

Same treatment Enter got in ADR 0027: `Qt::Key_Backspace`/`Key_Delete`
no longer set `isEdit = true`, so they're excluded from
`snapAnimationToTarget()`'s instant-render path and glide like
navigation when `animations` is on. The `isEdit` flag's remaining
(and now sole) purpose is plain character insertion — ADR 0017's
original reasoning (gliding can't keep pace with fast repeated small
jumps, reads as lag not smoothness) still applies there, and only
there now.

### Click precision — a real, confirmed bug

`offsetForPoint`'s column math was `localX / m_charWidth` — plain
integer division, which **floors** to whichever column's *left* edge
the click falls past, not the *nearest* column. A click in a
character's right half still resolved to that character, so the
cursor only advanced once the pointer had moved almost a full
character width further right than expected — exactly the reported
symptom. Fixed with the standard round-to-nearest adjustment:
`(localX + charWidth / 2) / charWidth`. Verified empirically, not just
by reasoning about the formula: clicking at a sequence of x-offsets
from a line's start showed the column boundary now falls right at the
character's midpoint (confirmed at the pixel level via screenshots),
not at its far edge.

### `Ctrl+A` select-all

New `EditorViewport::selectAll()`: collapses to one cursor, anchor at
buffer start, head at buffer end — composes for free with every
selection-aware operation that already just checks
`m_selectionAnchors[i] != m_cursors[i]` (copy, cut, delete, replace-
over-selection). Added to the Help panel's keybinding reference in the
same change.

### About panel content centered under the logo

`AboutPanel`'s body `QLabel` gets `Qt::AlignHCenter`, and each `<p>` in
its HTML template gets `align="center"` (Qt's rich-text subset
respects the attribute; the label's own alignment alone doesn't
reliably center multi-paragraph rich text). The header row (badge +
"About" title) stays left-aligned, matching every other panel's header
— only the logo/body content underneath, which is this panel's actual
subject matter, centers.

### Synced (but not identical) input-field caret blink

`QApplication::setCursorFlashTime(720)` in `main.cpp`, matching
`EditorViewport`'s own hard-blink cadence (a toggle every 12 ticks at
30ms = 360ms, i.e. a 720ms full on/off cycle). This is the one global
lever Qt exposes for every native `QLineEdit`'s blink rate, so every
floating panel's input field (`FindBar`, `FileBrowserPanel`,
`CommandLine`) now blinks at the same *rate* as the main editor caret,
instead of Qt's unrelated platform default. Documented as a partial
answer, not the full ask: Qt's native line-edit caret is always a hard
on/off toggle — it can't do the smooth fade `EditorViewport` itself
does with `animations = true`. Matching that fully would mean
replacing every `QLineEdit`'s own cursor painting (a custom subclass
overriding/suppressing native caret rendering), real work not
attempted here; flagged for a follow-up if the rate match on its own
doesn't read as "global" enough.

## Consequences

Verified live via `xdotool`: Backspace merging two lines still
produces exactly the expected buffer content (save-to-file round
trip) — the animation-path change didn't touch correctness; a
systematic sequence of clicks at increasing x-offsets confirmed the
column boundary now sits at each character's midpoint; `Ctrl+A`
selects the whole buffer (status bar reports the cursor at the final
line/column, selection overlay visible across every line); the About
panel's body text now visibly centers under the logo. Full test suite
unaffected (GUI-only) — 9/9 still passing.

Not built: a fully custom-painted (fade-capable) caret inside native
`QLineEdit`s — see the synced-blink section above. Vim mode remains
the next planned phase.
