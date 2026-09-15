# ADR 0077: Replace mode (`R`)

## Status

Accepted

## Context

`r` replaces one character ([ADR 0076](0076-replace-a-character.md)).
`R` keeps replacing until Escape, which is a mode rather than a single
change and so needed more than another call into that.

## Decision

### A flag on Insert, not a fourth mode

`m_vimReplacing` sits alongside `VimMode::Insert`, the same call
[ADR 0056](0056-linewise-visual-tab-motion-searchable-shortcuts.md) made for linewise
Visual. Everything that matters — the caret shape, the gate that stops
Normal-mode keys reaching the buffer, the dot capture, Escape's step
back a column — already keys off "are we inserting". Only two paths
differ: typing, and Backspace.

The status bar says `REPLACE` rather than `INSERT`, because the
difference between them is exactly what a mode indicator is for.

### Verified against vim, case by case

vim is installed here, so each rule was checked by running the same keys
through `vim -u NONE -N` and diffing the buffer:

| keys | buffer | result |
|---|---|---|
| `Rxy<Esc>` | `abcdef` | `xycdef`, cursor col 2 |
| `Rxyz<BS><Esc>` | `abcdef` | `xycdef` — Backspace **restores** `c` |
| `Rxy<BS><BS><Esc>` | `abcdef` | `abcdef` — fully restored |
| `4lRxyz<Esc>` | `abcdef` | `abcdxyz` — **appends** past the line end |
| `3Rab<Esc>` | `abcdefgh` | `abababgh` — the count repeats the text |
| `2Rxy<Esc>` | `abcdefgh` | `xyxyefgh` |
| `R` on an empty line | | appends |

All of them match, as do `.` repeats of a session with and without a
count.

### Backspace restores rather than deletes

This is the rule that shapes the data. Each typed character records what
it overwrote, so Backspace can put it back; an empty record means that
character was appended past the end of the line and there is nothing to
restore, so Backspace just removes it. Without the record, undoing a
mistyped `R` would eat the line instead of repairing it.

### The count's extra passes run before the cursor steps back

Escape does two things on the way out, and their order is load-bearing.
`3Rab` first replays `ab` twice more from where typing stopped, *then*
steps the cursor back a column. Doing it the other way round starts each
extra pass one character early and produces `aababfgh` instead of
`abababgh`. Found by diffing against vim, not by reading the code.

### `.` replays through the overwriting path

The dot machinery captures the typed text
([ADR 0075](0075-repeating-the-last-change.md)) and re-inserts it on
replay. For `R` that would insert rather than overwrite, so the replay
checks the flag and routes through the same per-character replace the
live session uses.

## Consequences

Undo granularity is per typed character, not per session: `Rxy<Esc>u`
restores one character where vim restores the line. This is **not** new
— plain Insert already behaves this way (`ixy<Esc>u` leaves `xabcdef`
here against vim's `abcdef`), and `R` inherits it rather than
introducing it. Making an insert or replace session a single undo group
is a real improvement and a separate change, since it would alter `i`,
`a`, `o` and `c` as much as `R`.

Visual-mode `r`, which replaces every character in a selection, is still
not implemented — the same gap Visual-mode operators have in
[ADR 0075](0075-repeating-the-last-change.md).

`R` does not write a register, for the same reason `r` does not.
