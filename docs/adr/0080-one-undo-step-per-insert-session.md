# ADR 0080: One undo step per insert session

## Status

Accepted

## Context

Undo granularity was one step per keystroke. `ixyz<Esc>u` left `xyabcdef`
where vim leaves `abcdef`; undoing a sentence meant holding `u`.

This was not a vim-mode problem. Every `insertText()` opened and closed
its own group, so it applied to ordinary typing as much as to `i`.

## Decision

### The session owns the group; the per-keystroke ones stand down

The core undo stack does not nest groups — `ase_undo_begin_group()` is
ignored when one is already open, and a matching `end_group()` would
close the outer one early
([core/include/ase/undo.h](https://github.com/saeeedhany/ase/blob/main/core/include/ase/undo.h)). So rather
than adding a depth count to the core, the GUI routes all fourteen of
its group sites through `beginUndoStep()`/`endUndoStep()`, which do
nothing while a session is open.

`beginUndoSession()` opens the one group that matters, and the Escape
that ends the insert closes it. The cursors recorded either side are
then where typing started and where it stopped, which is where vim's
undo puts you back.

### The session opens before anything the command changes

`o` inserts a line break and *then* enters Insert. Opening the session
after that left the break outside the group, so undo removed the typed
text and left a blank line behind. Same for `O`, and for `c`, which
deletes a range before entering Insert — vim undoes a `c` and the typing
that follows it as one.

So the session opens first, ahead of the mutation and ahead of the
cursor move, for every insert-entry command.

## Consequences

Checked against vim 9.2: `i`, `a`, `A`, `o`, `O`, `c`, `cc` and `R`
sessions all undo in one step and match, `u` twice steps back past the
session as it should, and `Ctrl+R` redoes it. `x` and `dw` are
unchanged, being single changes already.

A session left open by something other than Escape — closing a buffer
mid-insert, say — leaves the group open until the next Escape. Nothing
is lost, since the entries are recorded either way, but the grouping
would be wider than intended. Escape is the only way out of Insert
today, so there is no path to it now.
