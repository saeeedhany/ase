# ADR 0089: `:q` closes a buffer, and closing stopped sleeping

## Status

Accepted

## Context

Two complaints about the same moment — closing something.

`:q` called `window()->close()`. It quit the whole editor whatever was
open, which is not what `:q` does in the editor it is imitating, and
there was no `:q!` at all. `Ctrl+W` already closed a single buffer, so
the two bindings for one action disagreed about what the action was.

Separately, closing a file with a language server attached took a
visibly long moment. Reported as noticeable rather than terrible, which
is the range where nobody profiles and everybody assumes the language
server is just slow.

## Decision

### `:q` closes the current buffer; the last one closes the window

`Ctrl+W`'s behaviour, reached by the other binding — the same
two-bindings-one-action shape as `gd`/`F12`
([ADR 0067](0067-go-to-definition.md)) and `/`/`Ctrl+F`
([ADR 0074](0074-search-with-slash-and-n.md)). The window owns the
buffer list, so the viewport emits `closeRequested(force)` rather than
deciding: what closing the last buffer means is not the buffer's
question to answer.

`:q!` discards unsaved changes instead of prompting. The force flag is
checked before the "is this the last buffer" branch, so `:q!` on a
single dirty buffer skips the prompt on the way out rather than
escaping the buffer-level one and meeting the window-level one
immediately after.

### The close path polled instead of sleeping

Measured, not guessed: tearing down a clangd session blocked the UI
thread for **210 ms**, and the same 210 ms whether the file was 11 KB or
277 KB. That constant is the tell — the cost had nothing to do with the
file, the server, or the work either was doing.

`platform_terminate()` closed the child's pipes, asked once whether it
had exited, and on "not yet" slept a flat 200 ms before asking again. A
child whose stdin just closed exits in a few milliseconds, so the sleep
was almost entirely spent waiting after the thing it was waiting for had
already happened. Polling every millisecond within the same 200 ms
budget took the teardown to **19 ms**, and clangd still exits cleanly on
its own — the kill path is a fallback that now rarely runs.

`wait_for()` in the LSP client had the same shape one layer up: poll,
then sleep a flat 10 ms. It now starts at 1 ms and backs off to 10,
which keeps a reply that is already on the pipe from costing a fixed
10 ms. This is on the open path too, via the `initialize` handshake.

The general rule, since this is the second instance in one file: **a
fixed sleep on the UI thread is a bug unless something is known to take
that long.** Poll at the resolution the answer arrives at, and keep the
long number as a timeout rather than as a delay.

## Consequences

The test suite went from 0.57 s to 0.15 s, because the process tests
were paying the same 200 ms each. That it showed up as a *test* speedup
first is a fair hint about how long it had been there.

19 ms is below the threshold where anyone notices, so the teardown stays
synchronous. Moving it off the UI thread would buy the remaining
milliseconds at the cost of threading a deliberately single-threaded C
core, which is not a trade worth making for this.

`:q` on the last buffer still closes the window, so a user who types it
out of habit expecting to quit is not surprised. The reverse — someone
who expects vim's "close this split" — gets that too, until there is one
left. There are no splits yet, so the ambiguity has not had to be
resolved.
