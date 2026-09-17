# ADR 0127: The buffer with nothing to fall back on

## Status

Accepted

## Context

ADR 0110 gave unsaved work a snapshot that survives a crash. It filed
each snapshot under the buffer's file path, and both
`armRecoverySnapshot()` and `writeRecoverySnapshot()` opened with

```cpp
if (m_recoveryDir.isEmpty() || m_filePath.isEmpty()) {
    return;
}
```

So a buffer with no path was never snapshotted at all. `Ctrl+N`, type for
an hour, crash, and everything is gone — the one case where there is no
file on disk to fall back to, and the only one the feature did not
cover.

The second gap was the other end: nothing ever removed a snapshot for a
file crashed on and never reopened, so `<config>/recovery/` grew for
good.

## Decision

### An identity that is not a path

A snapshot is filed under a *key*. For a saved file that is still its
path. For a buffer without one it is `untitled:<pid>:<serial>`.

The pid keeps two running editors apart; the serial keeps two untitled
buffers in one editor apart. No absolute path begins `untitled:`, so
nothing can collide with a real file.

`saveAs()` drops the snapshot *before* moving to the new key, or the
untitled one would be left behind and offered back later as work that
was lost.

### Offered at startup, because nothing will reopen it

A named file's snapshot is found when you open that file. An untitled
one has no such moment, so the window walks the directory at startup and
asks.

`ase_recovery_list()` is that walk. It reads each file's recorded key, so
anything in the directory that is not a whole, well-formed snapshot —
truncated, or not one at all — is skipped rather than becoming a phantom
entry in a prompt.

A key whose pid still belongs to a running process is left alone. The
alternative is that opening a second window offers back a buffer that is
on screen in the first one.

### A clean exit is not a crash

Closing a buffer you were asked about, or quitting past the
unsaved-changes prompt, now drops the snapshot. Work you were asked about
and chose to drop is not work the editor lost, and leaving it would offer
it back at the next start having just been told to discard it.

### Pruning

`ase_recovery_prune()` removes snapshots untouched for 30 days, run at
startup. A snapshot from the future is not an old one, so a clock that
moved backwards deletes nothing.

## Consequences

Recovering an untitled buffer opens it as unsaved — it has content and
nothing on disk, so the dot is the truth. The undo stack cannot say so,
because the text goes in before there is one, so it is marked through
the same flag `Ctrl+W`-mid-insert already used.

A bare launch leaves one empty untitled buffer; recovering into a new tab
beside it left that as clutter, so it is closed, but only while it is
genuinely untouched.

Verified against a real `SIGKILL` rather than a simulated one: type into
an untitled buffer, `kill -9`, restart, and the prompt appears and the
text comes back. The same run confirms a clean `:q!` leaves nothing, that
Discard removes it, and that a second instance is not offered the first
one's live buffer.

Three tests in `gui/tests/test_recovery_flow.cpp` cover the viewport
half; they fail on the old code with `hasRecoverySnapshot()` returning
false. Four in `core/tests/test_recovery.c` cover listing and pruning.

Writing them turned up that the suite's cleanup only `rmdir`'d, which
fails silently on a non-empty directory — so a failed run left debris
that broke a *different* test next time, which is exactly what happened
once here. Cleanup now removes the files, and the test that needed "the
only snapshot in the directory" asks for its own by key instead.
