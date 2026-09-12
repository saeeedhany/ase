# ADR 0051: Global animation consistency, and three Vim-mode polish fixes

## Status

Accepted

## Context

Direct bug reports after using ADR 0047–0050's work for real: Visual-mode
navigation didn't glide at all; `Shift+A` (jump to end of line, enter
Insert) showed no transition; the typing pop-in animation (ADR 0049)
"doesn't work at all"; and three more asks — Visual mode should use the
same block cursor as Normal, the block cursor's capped opacity (ADR
0047) was wrong and should be full/normal, and `:` should open the
command line directly in Vim mode instead of requiring `Ctrl+;`.

## Decision

### The typing pop-in's real bug: `snapAnimationToTarget()` was clearing its own animation

`insertText()` pushes a fresh `TypingAnimation` entry, then — for the
plain-character and Tab cases — `keyPressEvent` called
`snapAnimationToTarget()` right after. ADR 0049 had made that function
also clear `m_typingAnimations` (reasoning: an undo/redo or Vim command
shouldn't let a stale pop-in keep animating over content it's no longer
about). The result: every single typed character's animation was wiped
in the same keystroke that created it, before a single frame ever
rendered any progress. The earlier "verified live" screenshot in ADR
0049 was a false positive — it compared the typed character's
brightness against the gutter line-number's own (deliberately dimmed)
alpha tier, not against that same character at a later frame, so a
real regression read as a pass.

Fixed by moving the clear to only where it's actually needed:
`applyUndoResult()` (shared by `undo()`/`redo()`), the one case that
truly can invalidate an in-flight animation's byte range with unrelated
content. `snapAnimationToTarget()` itself no longer touches
`m_typingAnimations` at all.

### Typing now glides too, not just the character pop-in

Separately, `keyPressEvent` had an `isEdit` flag that forced plain
character insertion and Tab to snap the *caret position* instantly
(ADR 0017's original reasoning: gliding can't keep pace with fast
repeated small jumps). Direct instruction this round: make animation
consistent everywhere, including typing. Removed `isEdit` entirely —
typing's caret now eases through `m_renderedCaretPos` like every other
cursor move (arrows, Backspace, Enter already worked this way).

### Vim mode was snapping after almost everything

The actual cause of "Visual mode doesn't animate" and "Shift+A doesn't
animate": `handleVimNormalOrVisualKey()` called `snapAnimationToTarget()`
after *every* resolved key — plain motions (`h j k l 0 ^ $ w b e`,
`gg`, `G`) in both Normal and Visual mode, the Visual-mode operator
switch (`v x d y c`), and the Normal-mode mode-entry/operator switch
(`i a I A o O x p P u`, and starting `d`/`y`/`c`). Every one of these
forced an instant snap regardless of `animations`, unlike arrow-key
navigation, which never did. Removed the call from all of these paths,
plus from the Vim mutation primitives that had their own
(`vimDeleteRange`, `vimDeleteLines`, `vimPasteAfter`, `vimPasteBefore`,
`vimOpenLineAbove`) and from `runCommand`'s `:<digits>` ex-jump — all
of Vim mode now glides exactly like the rest of the app, with no
special-cased exceptions left. `undo()`/`redo()` keep their snap
(a restored cursor position can be anywhere in the document — an
arbitrary jump, not a discrete one-step motion — so instant sync
remains the sensible default there, matching every other
non-Vim structural reload in this file, e.g. `openFile`).

### Visual mode gets the block cursor too

`blockCursor`'s condition widened from `m_vimMode == VimMode::Normal`
to `m_vimMode != VimMode::Insert` — Normal and Visual now render
identically (Insert keeps the bar, since it's still "an insertion
point"). Visual's own selection highlight and the block cursor
compose fine together; no rendering changes needed beyond the
condition itself.

### Block cursor: full opacity, not capped

ADR 0047's `kVimBlockCursorMaxAlpha = 200` (~78% peak) is gone —
direct feedback called the capped-opacity decision wrong. The block
now uses the exact same `caretAlpha` (0–255, full breathing range) the
bar caret always used. The knocked-out glyph drawn on top (also
unchanged) already guarantees the character stays legible regardless
of the block's opacity, so nothing else needed to change.

### `:` opens the command line directly in Vim Normal/Visual mode

Real vim's own convention. Added as a new check near the top of
`handleVimNormalOrVisualKey()` (alongside `g`/`G`/motions, before the
mode-specific branches, so it works identically from Normal or
Visual): `c == ':'` calls `m_commandLine->openCommandLine()`. Purely
additive — the existing app-wide `Ctrl+;` (ADR 0025) still works
everywhere, Vim mode or not; this just adds the more natural
Vim-idiomatic bare-colon trigger where it's otherwise a dead, swallowed
key. Insert mode is untouched — a bare `:` there still types as a
literal character, since this check only runs inside Vim's own
Normal/Visual dispatch.

## Consequences

Verified live: a `G` jump (5 lines) sampled across three frames shows
the block cursor genuinely mid-transit at 20ms, further along at 50ms,
and settled by 300ms — real interpolation, not an instant jump with a
lucky screenshot. The identical motion from Visual mode shows the same
progressive glide with the selection extending underneath it. A typed
character sampled at 20ms vs. settled shows a visibly dimmer/smaller
glyph early and a fully saturated one once settled — the pop-in
genuinely renders progressively now. Visual mode shows the same block
cursor as Normal, at full (uncapped) opacity. `:` opens the command
line from Normal mode; the same keystroke in Insert mode still inserts
a literal `:`. `ctest --test-dir build` still 9/9 throughout (no
core-layer change).
