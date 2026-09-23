# ADR 0142: Four things a plugin can react to

## Status

Accepted

## Context

[ADR 0141](0141-what-a-plugin-is-handed.md) gave a plugin the editor
rather than just its text, but only when asked: a command runs because
somebody typed its name or pressed its key. Nothing a plugin writes can
run because *something happened*.

That rules out the plugins people most want: format on save, lint on
change, a status widget that follows the caret, project tooling that
wakes when a file opens.

## Decision

Four events, and the number is the decision as much as the list is:

| event | when |
| --- | --- |
| `buffer_changed` | the text may have changed |
| `cursor_moved` | the caret is somewhere else |
| `file_saved` | the file is on disk |
| `file_opened` | a buffer is ready to be worked on |

Each one is something plugins actually want. Every additional event is a
promise about exactly when it fires, and that promise has to hold
forever, so the bar for a fifth is high.

A hook has a command's signature. A hook *is* a command nobody typed,
and it gets the same `AseEditorContext`.

### Coalesced, because the alternative is typing latency

`buffer_changed` would naturally fire from `refreshCache()`, which is
the per-keystroke choke point, on the thread that draws. A hook taking
5ms would then cost 5ms per keystroke.

Instead the two hot events are coalesced onto a 50ms single-shot timer.
A burst of typing is one `buffer_changed`; holding `j` is one
`cursor_moved`. They describe what changed since the last one rather
than every step in between.

**The timer only exists while something is listening.** `schedule()`
returns immediately when no hook is registered for either event, so an
editor with no plugins never starts it and pays nothing.

**The caret is compared, not flagged.** There are 56 places that move
it, and instrumenting all of them is 56 chances to miss one. The flush
compares the current offset against the last one reported — which also
means a caret that ends where it started correctly did not move.

### What a hook does is not itself an event

The recommendation asked for this to be decided rather than discovered
as a crash. Two layers:

**In the host**, one re-entrancy flag for *all* events, not one per
event. A per-event flag stops `buffer_changed` calling itself but not a
`buffer_changed` hook that moves the caret whose hook edits the buffer —
two hooks bouncing forever. One flag makes any chain terminate. Both
cases are tests, and the single-event one takes the stack out without
the guard.

**Across the timer**, the same rule, and here it is load-bearing rather
than defensive: `emitPluginEvent()` calls `refreshCache()` afterwards,
which sets the buffer-changed flag. Without suppression a hook that
edits would raise the event that called it one tick later, forever.

Only the hook's *own* contribution is taken back. Clearing the flags
outright was the first attempt and it was wrong: the deferred
`file_opened` hook ran during the first event-loop spin and swallowed
the typing that had happened before it. The pending state is saved
across the emit and restored.

A related case, found the same way: `runPluginCommand()` calls
`refreshCache()` whether or not the command changed anything, so running
*any* plugin command looked like typing. It already knows whether the
text changed, and now says so.

### A hook that edits is one undo step

A hook edits the `AseBuffer` directly, underneath the undo stack, which
is the problem [ADR 0128](0128-one-name-one-command.md) solved for
commands and which hooks reintroduced — they were not recorded at all.
The put-back-and-redo is now one function both use.

### `file_saved` fires after the write, and may cause a second one

Firing before the write would make the name a lie. Firing after it left
a formatter's output in the buffer and the *unformatted* text on disk —
verified by looking, which is how this was caught: the editor showed
stripped whitespace and `cat -A` showed the file still had it. Two saves
to save once is not format-on-save.

So the file is written again when a hook changed the buffer, and the
buffer ends clean. Once, not until it settles: a hook's edit does not
raise `file_saved`, so the second write cannot call the hook again.

## Consequences

Nineteen tests. Five in core for the registry, ordering, the two
re-entrancy shapes and the arguments nobody should pass; five in the GUI
driving a real editor with a real Lua plugin — a burst of twelve
keystrokes is asserted to be exactly one event, `j` `k` back to the
start exactly none.

Verified in the running editor with a strip-trailing-whitespace plugin:
one `:w` left the file clean on disk, the status said it ran once, the
tab showed no dirty dot, and `u` took the strip back.

What this does not do: hooks are synchronous and on the UI thread, so a
slow hook is still a slow editor. The guide says "must be fast" because
nothing enforces it. Making hooks async would need a thread the plugin
API does not have and an answer for what a hook sees when the buffer
moves underneath it — a bigger design than this one.

`buffer_changed` means the text *may* have changed: `refreshCache()` is
the signal and a few of its callers rebuild without an edit. A cursor
move alone never raises it, which is the distinction that matters.
