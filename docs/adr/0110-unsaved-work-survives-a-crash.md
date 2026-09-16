# ADR 0110: Unsaved work survives a crash

## Status

Accepted

## Context

ADR 0109 made a save that goes wrong stop destroying the file. That is
half of the "never loses user data" pillar in `docs/SPEC.md`; the other
half is the work that never reached a save at all. Killing the editor
with unsaved changes lost them, exactly as it always had.

Probing that turned up a second, worse problem. `isDirty()` compared the
undo stack's state id against the one recorded at the last save, and a
state id only appears when an undo group is **committed**. Insert mode is
one long open group, so a buffer being actively typed into reported
itself unmodified:

- the dirty dot in the buffer bar stayed off
- the `*` in the status bar stayed off
- `Ctrl+W` closed the buffer **without asking**, and the work was gone

Verified in the running editor: typing `WOULD BE LOST` into a file and
pressing `Ctrl+W` closed it silently, leaving the file as it was. That is
not a crash-safety gap, it is ordinary data loss on an ordinary keypress.

## Decision

### `ase_undo_has_uncommitted()`, and `isDirty()` uses it

An open group that has already recorded an edit means the buffer differs
from every committed state. `has_pending` alone is not enough — a group
is opened before anything is recorded into it, and an empty one has
changed nothing.

This is the fix for the silent close, and it is what makes a snapshot
possible at all: the snapshot is gated on the buffer being dirty, and
mid-insert is exactly when the work is most at risk.

### A snapshot, not a journal of edits

`core/src/recovery.c` records the whole buffer rather than a log of
edits. Replaying a log is a second implementation of editing that can
disagree with the first; a snapshot cannot disagree with anything,
because it *is* the buffer. It costs a full write where a log would cost
an append, and that is the whole of the trade.

Measured, since the last ADR was about exactly this kind of cost: 0.15 ms
at 113 KB, 0.96 ms at 1 MB, 4.43 ms at 4.5 MB — inside a frame, and once
per pause in typing rather than per keystroke. No size cap is needed. The
`fsync` in that figure is against this machine's disk; a slower device
will cost more.

Snapshots live in `<config dir>/recovery/`, named after a hash of the
file's path, so they never appear beside the user's own files and cannot
be committed by accident. The path is recorded inside each snapshot and
checked on read, so a hash collision reads as "no recovery" rather than
as somebody else's text. They are written through `ase_write_atomically`,
so an interrupted snapshot leaves the previous one intact.

### Written on a pause, removed on save, removed on a clean exit

Every edit restarts a 900 ms timer, so a burst of typing produces one
snapshot at the end of it. Saving removes it: the file is the work now.
So does the destructor — closing normally means the work was saved or
deliberately dropped, and only a crash leaves a snapshot behind, because
only a crash skips the destructor.

### Asked, not restored silently

On finding a snapshot the editor asks, after the window has painted so
the on-disk version is visible behind the dialog. The file may have been
changed by something else since the crash, and only the user knows which
version they want. **Recover** is the default because it is the
reversible answer: the restore is an ordinary undoable edit, so `u`
returns to what is on disk. Discarding is not reversible.

## Consequences

Nine core tests and nine in-process GUI tests. The core ones were
mutation-tested: dropping the recorded-path check, writing the content as
a C string, giving every file the same snapshot name, and skipping the
directory creation are each caught. The last one was **not** caught at
first, because the suite left its directory behind and a second run found
it already there — the same litter that put `test_starter_doc.tmp` in the
repository. Fixed, and the mutation is caught now.

The whole chain was then run for real: type without saving, `SIGKILL` the
editor, relaunch, take the default, save. The file ends up with the work
that never reached disk.

Two limits, both deliberate:

**A buffer with no path gets no snapshot.** The snapshot is keyed on the
file path, and an unnamed buffer has none to key on. That is the case
where unsaved work is most exposed, and it is not covered here.

**Snapshots are never pruned.** One only exists for a file the editor
crashed on, and it is removed the next time that file is opened. A file
crashed on and never reopened keeps its snapshot indefinitely.

Opening the same file through a symlink and through its real path keys
two different snapshots, since the key is the path as given.
