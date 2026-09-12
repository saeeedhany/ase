# ADR 0046: Native Vim mode, Phase 1

## Status

Accepted

## Context

`docs/ROADMAP.md` listed Vim mode as the last item before v1 — "full
modal emulation, not a lighter subset" — unblocked once undo/redo
(Phase 10) and the multi-cursor selection model (Phase 11) existed,
with the `:` command line (Phase 15) intended to become the ex-command
line.

**Native vs. plugin**: the plugin ABI (`docs/adr/0038`-era design)
only exposes discrete, named commands invoked programmatically — there
is no raw-keystroke or modal-state hook a plugin could use to
intercept every key the way modal editing needs. Building that hook
just to support one feature, then routing every keystroke through the
Lua FFI boundary, would be slower and more awkward than the
alternative: build the modal engine directly into `EditorViewport`,
which already owns the cursor/selection/undo model these commands need
to reach.

Real Vim's full surface is enormous. This ADR scopes a **Phase 1**: a
complete, genuinely usable modal-editing slice — not a partial attempt
at everything — with the rest of the surface explicitly deferred and
named.

Two facts confirmed directly against the code before designing this:
`cutSelection()` already used the exact shape Vim's operators need —
one `ase_undo_begin_group`/`end_group` pair wrapping direct buffer
calls, not nested calls to another batch entrypoint (undo groups can't
nest, `core/src/undo.c`) — so no new undo capability was needed, just
new methods with the same shape. And `statusChanged(int, int, bool)`
had exactly one declaration, one emit site, one connect site — safe to
extend its signature without a wider audit.

## Decision

### Data model: inline in `EditorViewport`, no new class

New state is private members on `EditorViewport`, not a separate
`VimEngine`. This project's actual extraction precedent is "a
self-contained widget with its own paint/animation state gets its own
class" (`TrackingPopup`, `CompletionPopup`); logic that reaches
`m_cursors`/`m_cache`/`m_buffer`/`m_undo` directly stays inline — the
same place the existing cursor primitives and the whole LSP/completion
subsystem already live, since neither of those has an independent
widget of its own either.

```cpp
enum class VimMode { Insert, Normal, Visual };
VimMode m_vimMode = VimMode::Insert;   // default Insert == identical to "off" everywhere but one gate
bool m_vimModeEnabled = false;         // from config

int  m_vimCount1 = 0;                  // count before an operator/motion
char m_vimPendingOperator = '\0';      // 'd' / 'y' / 'c'
int  m_vimCount2 = 0;                  // count after the operator
bool m_vimPendingG = false;            // mid-"gg"
bool m_vimLastYankWasLinewise = false; // drives p/P placement
```

### `keyPressEvent` integration

`Key_Escape` is mode-aware: Insert → step left one char (if not at
column 0) then enter Normal; Normal/Visual → clear pending Vim state
and `collapseToOneCursor()`. A new gate right after the existing
`m_desiredColumn = -1;` line hands off to
`handleVimNormalOrVisualKey(event)` whenever
`vimModeActive() && m_vimMode != Insert`; it collapses to one cursor
first (Vim operates on the primary cursor only — a stray Ctrl+D
fan-out self-corrects on the next Vim keystroke), then claims Vim's
recognized keys and swallows any other unmodified printable key.
`Tab`/`Return`/`Delete` get a one-line guard so they no-op in
Normal/Visual instead of mutating the buffer; the plain-character
insert path gets the same guard.

**Arrow keys and the entire Ctrl-modifier chain stay unconditional on
Vim mode** — Ctrl+S/Ctrl+Q/Find/Ctrl+D/etc. never become mode-dependent
(also preserves ADR 0026's "quit is a single, deliberate action"
ruling by construction). One addition: `Ctrl+R` → `redo()`, gated on
`vimModeActive()` so non-Vim users see no new shortcut.

### Phase 1 scope

**Modes:** Insert, Normal, Visual (charwise). **Entry/exit:**
`i a I A o O` → Insert; `Escape` → Normal; `v` toggles Normal↔Visual.

**Motions** (repeatable by count): `h j k l`, `0`, `^` (first
non-blank), `$`, `gg`, `G`, `w b e` (three-class word scan — see
below).

**Operators:** `d y c`, composable with any motion, with counts on
either side (`3j`, `d2w`, `2dw`, `3dd`/`yy`/`cc` linewise). Plus `x`,
`p`/`P`, `u`/`Ctrl+R` (bound to the existing `undo()`/`redo()`).

**Visual mode:** motions extend the selection via the existing
`extend=true` path; `d y c x` act on the selection via
`hasSelectionAt`/`selectionMinAt`/`selectionMaxAt`.

**Ex-command:** `runCommand`'s existing if/else-if chain (still
triggered via this app's existing `Ctrl+;`, not a bare `:` — see ADR
0025) gained one new branch: `:<digits>` jumps to that line. Not gated
on `vim_mode` — proves the dispatch grows past exact-string match for
anyone, Vim or not.

**Explicitly deferred** (named, not silently missing): registers
(clipboard is the only register), macros, marks, text objects,
dot-repeat, jumplist (a future `Ctrl+I` would collide with the About
panel shortcut), `gq`, Visual Block/Line, Replace mode, single-char
`r`, `:s///`/`:g//`, `/`/`?`/`n`/`N` search-motion (FindBar is a
plausible future reuse), Insert-mode Vim commands, `J`, indent, case
ops.

**Keybinding customization is out of scope, project-wide, not just for
Vim.** Every existing shortcut in this editor is a hardcoded C++
constant — ADR 0026 states this explicitly. There is no config-driven
remapping system to plug into, so Vim's own bindings are hardcoded the
same way, for the same reason. Real rebinding — a config format,
conflict resolution against the existing Ctrl-shortcuts, a settings UI
or file syntax — is a separate, sizable feature spanning every
keybinding in the app, not a Vim-specific side effect. Worth its own
future ADR if it's wanted.

**Two named fidelity gaps against real Vim:**

- `dw`/`cw` at end-of-line behaves like a plain `w` (no special
  end-of-line word carve-out).
- `$`/`e` land where this editor's existing insertion-caret model
  already puts Home/End — the insertion *gap* just before the line's
  `'\n'` byte — not "on" the last visible character the way real Vim's
  block cursor does, since no block cursor exists here. This is a
  deliberate, accepted deviation, not an oversight — see the bug fixed
  below for the one place it actually mattered operationally.

### Word-motion algorithm (`w`/`b`/`e`)

A three-class model over `m_cache` — `Blank` (deliberately including
`'\n'`, so a motion crosses line boundaries with zero special-casing,
matching real Vim's own "a blank line acts like whitespace"
behavior), `Word` (per the existing `isWordChar`), `Punct` (otherwise)
— stepping by codepoint via two new helpers
(`vimNextCharBoundary`/`vimPrevCharBoundary`) built on the existing
`isUtf8ContinuationByte`. Lives in the GUI layer, matching where
`isWordChar`/`addCursorAtNextOccurrence` already live — no new
`AseBuffer` byte-range primitive needed, since `m_cache` already
mirrors the whole buffer.

### Operator+motion+count parsing

A small pending-state machine using a few member variables rather than
a formal state enum: digits accumulate into `m_vimCount1`; an operator
letter moves to "operator pending," where digits accumulate into
`m_vimCount2`, the same operator letter repeated fires the
linewise-doubled form (`dd`/`yy`/`cc`), and a motion key computes the
range and applies the operator once. `Escape` or any unrecognized key
clears all pending state. `j k gg G` are linewise when combined with
an operator; `h l w b e 0 ^ $` are charwise.

### Undo grouping: one group per Vim command

New mutation methods (`vimDeleteRange`, `vimYankRange`,
`vimChangeRange`, `vimDeleteLines`, `vimYankLines`, `vimPasteAfter`,
`vimPasteBefore`, `vimOpenLineAbove`) each open exactly one
`ase_undo_begin_group`/`end_group` pair around direct
`ase_buffer_insert`/`ase_buffer_delete` + `ase_undo_record_*` calls —
never through `insertText()`/`deleteBackward()` as batch entrypoints,
which would each try to open their own group (groups can't nest). Same
shape `cutSelection()` already used, simpler here since Vim mode is
single-cursor by construction.

### Config + status bar

`applyConfig()` reads `vim_mode` with the exact `animations` pattern
(`ase_config_get_string` + case-insensitive `"true"` compare, default
off — this changes what every keystroke does, so it must be opt-in).
`statusChanged` gained a 4th parameter, `modeLabel` (`"NORMAL"` /
`"INSERT"` / `"VISUAL"`, empty when Vim mode is off, so the status
text is byte-identical to before for anyone who hasn't opted in).
Plain text, no icon or color, per ADR 0007's one-font-color pillar.

### A real bug found and fixed during live verification: paste landing on the wrong line

`vimPasteAfter()`'s charwise branch initially called
`vimNextCharBoundary(m_cursors[0])` unconditionally to find the
insertion point — correct when the cursor sits "on" a real character,
since stepping past that character is exactly what paste-after means.
But at end-of-line, this editor's own insertion-point convention (the
fidelity gap named above) already places the cursor *at* the `'\n'`
byte itself, not on a real character before it. Applying
`vimNextCharBoundary` there stepped over the newline too, landing the
paste at the start of the *next* line instead of appending it to the
end of the current one — reproduced live with a `0 w v w y $ p`
sequence (yank "int ", paste at end of line) landing "int" prepended
to the following line.

Fixed by checking whether the byte at the cursor is `'\n'`: if so,
insert directly at the cursor (it's already the correct insertion
gap); otherwise step past the current character as before.
`vimPasteBefore()` was never affected — it always inserts directly at
the cursor, no forward step, so it had nothing to trip over.

## Consequences

Vim mode is a complete, real-editing-capable slice: motions, operators,
counts, Visual selection, single-step undo/redo, and ex-command line
jumps, verified live end-to-end (build clean with zero warnings on the
first attempt; `ctest --test-dir build` still 9/9). Off by default,
so it is invisible to anyone who hasn't opted in — every existing
shortcut, the status bar's format, and plain typing are unaffected
byte-for-byte when `vim_mode` is unset.

What's harder: the deferred list above is real Vim muscle-memory that
won't work yet (search-motion, dot-repeat, text objects, registers
beyond the clipboard) — worth tracking honestly in
`docs/ROADMAP.md` rather than implying full parity. The two named
fidelity gaps mean a small number of end-of-line edge cases won't feel
identical to real Vim; the paste bug above was the one case where that
gap had an actual behavioral cost, now closed.
