# ADR 0047: A real block cursor for Vim Normal mode

## Status

Accepted

## Context

Vim mode (Phase 1, `docs/adr/0046`) landed with the editor's existing
2px insertion-bar caret unchanged in every mode, including Normal. In
real Vim (and every terminal Vim runs in), Normal mode is unmistakable
at a glance because the cursor becomes a solid block covering the
character it's "on" — the editor's own bar caret gave no such cue,
so Normal mode looked visually identical to plain text editing. User
feedback after using Phase 1: give Normal mode a real block cursor,
make sure the character under it stays legible, keep the block's
blink from ever hitting full opacity, and make sure the existing
position-glide/blink-reset animation still drives it through every
motion and edit, not just a static highlight.

## Decision

### Block only in Normal mode

Insert keeps the bar — it still means "an insertion point," same as
with Vim mode off. Visual keeps it too: Visual already highlights the
selection range itself (translucent overlay, existing
`highlightRange`), and the bar there just marks that selection's
moving end without a second, competing fill on top of it. Only
`vimModeActive() && m_vimMode == VimMode::Normal` switches the caret
render path.

### Glyph width, measured the same run-aware way as everything else

The block's width is the actual rendered width of the character under
the cursor — not a fixed `m_charWidth` guess, which is exactly the
"assumed fixed-pitch" bug ADR 0013 already fixed once for the bar
caret and mouse hit-testing. `vimBlockGlyphAt()` (`editor_viewport.cpp`)
reuses `vimNextCharBoundary` (codepoint-aware, so a multi-byte UTF-8
character is measured whole) to find the glyph's end, then measures it
with the same `capturesForLine`/`xForColumn` machinery `drawLine`
itself uses — so a bold/italic capture under the cursor gets the
correct width and font, not the plain font's.

At end of line or end of buffer there's no real character to cover —
this editor's own cursor convention already sits *at* the line's `'\n'`
byte there (the named fidelity gap from ADR 0046), not "on" a
character. `vimBlockGlyphAt` returns an empty glyph and falls back to
`m_charWidth` for the block's width in that case, so the cursor still
renders as a normal-sized block instead of collapsing to zero width.

### The character stays legible: knock it out on top, terminal-cursor style

A block filled in the text's own color would simply hide whatever
character it lands on. Real Vim's terminal-cell block cursor solves
this by inverting the cell — the glyph renders in the background color
on top of the solid block. This editor does the same: after filling
the block rect, `vimBlockGlyphAt`'s returned glyph (if non-empty, i.e.
skipped for the end-of-line/buffer case, where there's nothing to
draw) is redrawn on top in `m_backgroundColor`, using that character's
own font (`fontForCapture`) so italic/bold glyphs still look correct
knocked out.

### Never fully opaque

`kVimBlockCursorMaxAlpha = 200` (of 255, ≈78%) caps the block's fill
alpha — the existing breathing-blink formula (`caretAlpha`, unchanged,
still driven by `m_idleTicks` and reset to solid on every
cursor-moving action via `resetCaretBlink()`) is scaled into
`[0, 200]` instead of `[0, 255]` before filling the block:
`blockAlpha = (caretAlpha * kVimBlockCursorMaxAlpha) / 255`. A solid,
fully-opaque block would flash harder than every other translucent
overlay this app already uses (selection highlight, panel
backgrounds) and would fight for attention with the knocked-out glyph
on top of it. The bar caret in Insert/Visual is untouched — still the
original uncapped `[0, 255]` range.

### Animation: inherited, not re-implemented

The block's position comes from the same `m_renderedCaretPos[i]`
eased values the bar caret already used (`caretTargetFor` +
`updateAnimation`'s per-frame glide toward it, `docs/adr/0015`) — no
new animation path. Its blink timing comes from the same `caretAlpha`
computation, itself driven by `m_idleTicks`, which every existing
cursor-moving/editing call site already resets via `resetCaretBlink()`
— including every Vim motion and mutation, since they all funnel
through `keyPressEvent`, which calls it unconditionally near the top
before any mode-specific dispatch. Nothing Vim-specific needed adding
here: giving Normal mode a block cursor was purely a paintEvent
rendering change, not a new animation system.

## Consequences

Normal mode is now visually unmistakable at a glance, matching every
Vim user's terminal-trained expectations, while Insert and Visual stay
exactly as they were. Verified live: the block correctly resizes
across different character widths and a styled (italic) capture,
knocks out and stays legible over a live character, falls back to a
normal-width block at end of line, visibly breathes (blink samples
across several frames showed clearly varying — never static —
intensity) while never reaching the bar caret's full peak brightness,
and glides/updates correctly through motions (`l`), edits (`x` +
`u` undo), and mode transitions. `ctest --test-dir build` still 9/9.
