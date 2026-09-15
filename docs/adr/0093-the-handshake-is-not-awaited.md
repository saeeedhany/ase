# ADR 0093: The initialize handshake is not awaited

## Status

Accepted

## Context

[ADR 0092](0092-a-discarded-server-is-not-waited-for.md) stopped the UI
thread waiting for a language server to *die*, and named what was still
wrong at the other end of the connection: it still waited for one to
*start*.

`ase_lsp_client_start` spawned the server, sent `initialize`, and then
blocked in `wait_for(client, &state, 3000)` until the reply came. Because
`onActivated()` starts a buffer's server the first time you land on it,
that wait was paid on a keystroke: the first `Ctrl+Tab` to each buffer
measured **17–24 ms**.

The average was not the problem. The timeout was. A cold clangd, a large
project, a loaded machine — anything that made the handshake slow made
the editor unresponsive for exactly that long, up to a **three-second
freeze** on a keypress. Same shape as the two waits before it: a
UI-thread block on a duration nobody controls, bounded by a constant
someone guessed.

## Decision

### Send, and let the poll finish it

`ase_lsp_client_start` now spawns, sends `initialize`, and returns. The
reply is picked up by `ase_lsp_client_poll` — which already runs on a
200 ms timer for diagnostics — and the callback sends `initialized` and
marks the client ready. First activation measures **1.6 ms**.

### Everything sent before the handshake lands is queued

This is what keeps the change invisible to callers. A server may ignore
or reject anything sent before `initialize` completes, and the GUI calls
`did_open` immediately after `start`.

So `send_message` queues the framed body when the client is not ready,
and the initialize callback flushes the queue in order. `did_open`,
`did_change`, completion, hover and definition all keep working exactly
as before, whenever they are called. Only the handshake's own two
messages bypass the queue, plus `shutdown`, which goes out on a client
that may never have become ready.

Verified: a file opened with deliberate errors shows its diagnostics,
which can only happen if the queued `did_open` really reached the
server after the handshake.

### A server that never answers is the poll's problem too

The 3000 ms timeout did not disappear, it moved. `poll` marks the client
dead if it is still unready after 3000 ms — without blocking anyone.

A *missing* server is caught earlier and more cheaply: the child exits
immediately, `poll` reads EOF, and the client dies within one 200 ms
tick. That is faster than the old synchronous path, which sat out the
full handshake timeout to reach the same conclusion.

### `Starting` is a state, because it is one

The status bar shows `clangd…` while the handshake is in flight, then
`clangd`. Pretending a server is `Running` before it is would make the
indicator lie for exactly as long as the thing this ADR is about.

`pollLsp()` promotes `Starting` to `Running` on readiness, or to
`Failed` with a message naming the config key to check. Measured on a
bogus command: the failure appears as "fake-server not found — check
lang.c.lsp" in 2.16 ms, where it used to cost a 3-second freeze first.

## Consequences

**`ase_lsp_client_start` no longer returns NULL for a server that cannot
be executed.** It returns NULL only when the spawn or an allocation
fails; an unexecutable command spawns fine and dies on the first poll.
The lifecycle test asserted the old contract and now asserts the new
one — that the client starts, is not ready, and stops being alive once
polled.

`ase_lsp_client_is_ready()` is new API, because a caller that reports
readiness in a status bar needs to distinguish "handshaking" from
"working", while a caller that just sends messages does not have to care.

Nothing in the LSP client blocks any more, which is why `wait_for`, its
`WaitState`, and `sleep_ms` are all gone from the file — the last one
had been the only reason it needed a platform-specific sleep at all.

Diagnostics for a just-opened file now appear up to one 200 ms poll
later than before, since the handshake completes on that timer rather
than inside the keypress. Trading a 20 ms freeze for 200 ms of extra
latency on a background result is the right way round.
