# ADR 0109: Saving without losing the file

## Status

Accepted

## Context

`docs/SPEC.md` lists **Robust — "Never loses user data. Crash-safe
autosave/journaling"** as a non-negotiable pillar. The save path did the
opposite:

```c
FILE *f = fopen(path, "wb");   /* truncates the user's file here */
for (Piece *p = ...) fwrite(...);
```

`fopen(path, "wb")` empties the file before a single byte is written. A
write that fails part-way leaves whatever fitted, and the original is
already gone. The failure is reported honestly — the editor says "could
not write" — by which time there is nothing left to write over.

Demonstrated by capping the filesystem and saving a buffer too large
for it:

```
before save: 41 bytes on disk
save reported: FAILURE
after failed save: 8192 bytes on disk
original text still present: NO - DATA LOST
```

Disk full, a quota, a removable drive pulled mid-write, a crash, a power
loss: all the same outcome.

## Decision

Write beside the target, then rename over it. `rename()` replaces
atomically, so the file on disk is either entirely the old contents or
entirely the new ones.

The temporary goes in the same directory as the target, because
`rename()` is atomic only within one filesystem and `/tmp` is routinely
a different one. It is created `0600` and given the original's
permissions just before the rename, so the content is never briefly
readable by anyone who could not already read the file. The stream is
`fsync`ed before the rename and the parent directory after it, since a
rename is only as durable as the directory entry recording it.

Symlinks are resolved first. Renaming over a symlink replaces the link,
which is how an editor silently detaches a dotfile from the repository
it is checked into; saving writes what the link points at.

Windows gets `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`, because the
Win32 `rename()` fails outright when the destination exists, and
`_commit` in place of `fsync`.

## Consequences

Four tests pin the behaviour, and all four fail against the old
implementation: a capped write leaves the original intact, no scratch
file survives a save, permissions are preserved, and a symlink is
followed rather than replaced.

Two costs, both deliberate:

**Hard links are broken.** The renamed file is a new inode, so a file
with two names ends up with the edit under one of them and the old
contents under the other. Verified, not assumed. This is what vim does
by default, and what git does; preserving the link means writing in
place, which is the behaviour being removed. Atomicity wins because the
pillar is about not losing data, and a detached link loses none.

**One path can still lose data.** If no temporary can be created — a
directory that is not writable while the file inside it is — the save
falls back to the old truncating write. Refusing to save would strand
work the user cannot get out any other way, which is also data loss, and
the fallback is no worse than what every save did until now. It is the
only remaining hole and it is narrow.

This is half the pillar. "Never loses user data" now holds for a save
that goes wrong; **crash-safe autosave/journaling still does not exist**,
so work lost to a crash before a save is lost. That is the other half
and it is not done here.

`core/src/internal.h`, added when `ase_strdup` was deduplicated, now
carries the per-platform pieces. `ase_config_write_default_if_missing`
uses a truncating `fopen` too, but only after establishing the file does
not exist, so there is nothing there to destroy.
