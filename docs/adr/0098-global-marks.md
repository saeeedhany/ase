# ADR 0098: Global marks reuse the jumplist's entry

## Status

Accepted

## Context

[ADR 0097](0097-marks-and-macros.md) added marks and stored them as a
buffer-local `(line, column)`. It accepted `A-Z` but treated them like
`a-z`, and said what was missing: *"a global mark has to name a file as
well as a position, which is the jumplist's problem too and should be
solved once for both."*

This is that, and the prediction held — the thing a global mark needs to
store is exactly `MainWindow::JumpEntry`: a path, a buffer id for an
untitled buffer that has no path, a line and a column.

## Decision

### The window owns them, because only the window knows about files

`m_globalMarks` is a `QHash<char, JumpEntry>` on `MainWindow`. The
viewport cannot own them: the answer to `` `A `` may be a buffer that is
not the current one, or not open at all.

Setting and jumping go out as signals — `globalMarkSetRequested`,
`globalMarkJumpRequested` — the way `fileOpenRequested` and
`jumpRecorded` already do. The window fills in the file identity, since
that is the part it has.

Jumping reuses `restoreJump()` whole: it already switches to the buffer
if open, **reopens it by path if it was closed**, and positions the
cursor. It gained one parameter, `exact`, because `` ` `` wants the
column and `'` wants the line's first non-blank. A global mark jump
records a jumplist entry first, so `Ctrl+O` comes back across the file
change.

### An uppercase mark is stored locally as well

Not redundancy — it is what makes `d'A` work. vim's rule, confirmed by
running it:

| | vim | ase |
|---|---|---|
| `d'A`, mark in **this** buffer | deletes the range | same |
| `d'A`, mark in **another** file | `E20: Mark not set`, no change | `mark A not set`, no change |
| `` `A ``, mark in another file | switches file, line 2 | same |

An operator needs offsets in the buffer it is operating on, and a
position in another file has none. So the local copy answers operators,
the global one answers jumps, and the failure case is an error rather
than something surprising happening in the wrong file.

### One letter, one place

The cost of that second copy is that it can go stale: set `mA` in one
buffer, set `mA` in another, and the first buffer would still answer
`d'A` from a copy of a mark that has moved.

So setting a global mark clears that letter from every *other* buffer's
local marks. Verified: after re-claiming `A` in the second buffer,
`d'A` in the first reports "mark A not set" and changes nothing.

## Consequences

Global marks live as long as the window and no longer. vim persists them
in a viminfo file across sessions; there is no such file here, and
adding one is a larger decision about what state the editor keeps on
disk than this ADR should make.

A mark in a buffer that was closed still works if the buffer had a path,
because `restoreJump` reopens it — the same behaviour `Ctrl+O` has. A
mark in a closed *untitled* buffer is gone, and says so.

This work turned up a bug it did not cause: `l` moves past the end of a
line and wraps onto the next one, where vim stops at the last character
(`12l` from column 9 of a 19-column line gives column 19 in vim, and
line 3 column 1 here). Found because a test set a mark with `12l` and
the mark landed on the wrong line. Fixed separately — the point worth
recording is that it had been there since `l` was written and surfaced
only when something else depended on where the cursor actually was.
