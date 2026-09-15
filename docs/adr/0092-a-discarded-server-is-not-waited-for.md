# ADR 0092: A discarded server is not waited for

## Status

Accepted

## Context

[ADR 0089](0089-closing-a-buffer.md) took closing a file with a language
server from 210 ms to 19 ms and claimed the problem solved. It measured
an **idle** clangd — one that had been polled for a second and a half
with nothing to do.

Measuring `Ctrl+W` properly told a different story. Open a file, let its
server start, close it a few seconds later — the way anyone actually
works — and the buffer's destructor cost **241 ms**, all of it in
`ase_lsp_client_stop`. The original complaint that started this work was
"open a file with a lot of C lines, it launches the LSP, and then when I
close it, it takes a noticeable amount of time". That was never fully
fixed; it was fixed for the case that was easy to measure.

The cost also hid behind `deleteLater()`. `Ctrl+W` itself measured
1.27 ms, because the real work happens when the event loop destroys the
viewport. A handler that looks fast and a UI that stutters right after
it is exactly the shape that makes this kind of thing survive.

## Decision

### Three separate things were waiting, and only one mattered

Found by bisecting the 241 ms, not by reading:

1. **The `shutdown` round-trip.** The client sent `shutdown` and blocked
   up to 1000 ms for the reply. Removed — a client that is going away
   has no use for the answer. Worth ~0 ms here, but it was the 3-second
   worst case on a slow server.
2. **`platform_terminate`'s grace period.** It closed the child's pipes
   and polled 200 ms for an exit that never came, then killed it anyway.
   A busy clangd only looks at stdin between units of work, so closing
   the pipes did not reach it. It now sends **SIGTERM** — which the
   previous code never did — and bounds the wait at 20 ms.
3. **The final `waitpid`.** Still tens of milliseconds, because the
   kernel tears down clangd's address space before it returns.

Only removing all three gets to zero, and the third cannot be shortened
— only skipped.

### `ase_process_destroy_detached`

Nobody wants a discarded language server's exit status. So: close the
pipes, `SIGTERM`, remember the pid, return. Teardown measures
**0.03 ms**.

The pid is remembered rather than abandoned, and swept with
`waitpid(WNOHANG)` on the next spawn or destroy. Opportunistic because
this module has no timer and does not want one; a couple of zombies
between sweeps cost nothing, and the list is bounded — a full list falls
back to killing and waiting rather than leaking the pid.

`ase_process_destroy` keeps its blocking behaviour, because `:compile`
has `ase_process_has_exited()` / `ase_process_exit_code()` and a build's
exit status is the entire point of running it. The split is by whether
anyone wants the answer.

Verified: three open/close cycles plus app exit leave **zero** orphaned
`clangd` processes and zero zombies.

## Consequences

Closing a file is now effectively free regardless of what its server was
doing. The pattern across [ADR 0089](0089-closing-a-buffer.md),
[ADR 0090](0090-scrolling-runs-at-frame-rate.md) and this one is the
same three times over: **the UI thread waited on something whose
duration it did not control**, with the wait written as a constant
someone guessed. Every one of them measured as a suspiciously round
number — 210 ms, 30 ms a frame, 241 ms — and none was found by reading
the code.

A server killed 20 ms after SIGTERM may leave a partial background-index
shard. clangd writes those atomically and validates on read, so the cost
is re-indexing that file, not corruption.

`Ctrl+Tab` itself was measured at **0.8–1.2 ms** and needed nothing. The
first switch *to* a buffer still costs 17–24 ms, because
`onActivated()` starts that buffer's server and blocks on the
`initialize` handshake — the same class of bug as this one, at the other
end of the connection, and still unfixed. Its 3000 ms timeout is a
3-second UI freeze waiting to happen on a cold or slow server.
