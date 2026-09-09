# ADR 0011: LSP client — process isolation, sync handshake, async everything else

## Status

Accepted

## Context

Spec section 4: "LSP client module, isolated process boundary via
JSON-RPC over stdio — a misbehaving language server must never crash
the editor." Section 7 phase 6 scopes this to diagnostics, completion,
and go-to-definition — not full LSP spec coverage.

## Decisions

### 1. One child process per client, real process isolation

`ase_lsp_client_start` forks/execs the language server and talks to it
over two pipes (its stdin/stdout). No shared memory, no linking against
a server library — the server can do anything, including crash, and
the worst case is `ase_lsp_client_is_alive` going false. This is the
same isolation model the spec calls for and the same reasoning as
process-based (rather than in-process) plugin sandboxing would use, if
this project had that — here it's mandatory, not a choice.

**`SIGPIPE` is disabled process-wide** (`signal(SIGPIPE, SIG_IGN)`) the
first time a client starts. Without this, writing to a server that has
already died raises `SIGPIPE`, whose default action kills the whole
editor — exactly the failure mode this ADR exists to prevent. This is a
global, not per-client, side effect; documented here since it's the
kind of thing that's surprising to discover by accident later. Standard
practice for any program that manages pipes to child processes it
doesn't fully trust.

### 2. The initialize handshake is synchronous (bounded), everything else is async

`ase_lsp_client_start` blocks — polling with small sleeps, never a raw
blocking `read()` — for up to 3 seconds waiting for the `initialize`
response before returning. Simpler lifecycle: callers get back either a
working client or `NULL`, never a client that might still be mid-setup.
`ase_lsp_client_stop` does the same bounded wait for `shutdown`/`exit`
(1 second) before force-killing the process — never left hanging on a
server that won't cooperate.

Every other request (`textDocument/completion`, `textDocument/definition`)
is fire-and-return: the callback fires later, from inside
`ase_lsp_client_poll()`. Blocking Phase 2's GUI thread waiting on a
process that might be slow, hung, or gone would violate "no perceptible
input lag" far more visibly than a 3-second one-time startup delay
does.

### 3. `poll()` mirrors the config hot-reload shape: non-blocking, timer-driven

`ase_lsp_client_poll` never blocks — it drains whatever's available
from the non-blocking read end, tries to frame complete
`Content-Length`-delimited JSON-RPC messages out of the accumulated
buffer, and dispatches each. The intended integration (not built yet —
see "Not done" below) is the same shape as `EditorViewport`'s config
poll timer (ADR 0008): call it periodically from the GUI, no threading,
no locks, one clear place data crosses from "external process" to
"editor state."

### 4. Diagnostics via callback, requests via callback — same idea, different arity

`textDocument/publishDiagnostics` is a server-initiated notification
with no request behind it, so it gets a standing callback
(`ase_lsp_client_set_diagnostics_callback`), set once. Completion and
definition are request/response, so each call takes its own callback —
ordinary async-request shape. Both deliver JSON as *borrowed* pointers,
valid only for the duration of the callback, to avoid a whole class of
"who owns this JSON tree" bugs; a caller that needs the data longer
copies out of it.

### 5. Server-to-client requests are not implemented

If a server sends *us* a request (has both `method` and `id` — e.g.
`workspace/configuration`), v1 doesn't respond. Per LSP semantics, an
unanswered request degrades gracefully in a well-behaved server (it
just never resolves on their end); this deliberately doesn't attempt
the fuller LSP surface that would require answering server-initiated
requests correctly. Malformed/unrecognized messages of any kind are
logged nowhere and simply dropped — the framing layer, not this
dispatch logic, is what has to be bulletproof against garbage.

### 6. POSIX only in v1 — Windows returns NULL cleanly, not a half-tested attempt

Async child-process I/O on Windows needs either overlapped I/O or a
dedicated reader thread — meaningfully more machinery than any other
platform branch this project has written so far (contrast: config path
resolution, `_mkdir`, `LoadLibrary` — all a few lines each). Writing an
under-tested attempt at that risks shipping something that's *worse*
than an honest gap: silently-broken async I/O is a much worse failure
mode than a clean, documented "not supported yet." `ase_lsp_client_start`
detects this by simply having `platform_spawn` return `false` on
Windows — every other function in the file is consequently unreachable
there but still compiles (checked via the Windows CI job, which only
builds, doesn't run this functionally). Revisit when Windows LSP
support is an actual, prioritized goal, not before.

### 7. Tested against a fake language server, not a real one

`core/tests/fixtures/fake_lsp_server.c` is a ~100-line stand-in
implementing just enough LSP to be indistinguishable from a real server
for this client's purposes: replies to `initialize`, sends
`publishDiagnostics` after `didOpen`, replies to `textDocument/completion`
and `textDocument/definition` with fixed canned results, and handles
`shutdown`/`exit`. This is a deliberate choice over depending on a real
language server (e.g. `clangd`) being installed in every dev/CI
environment — the same reasoning as the plugin host's test fixtures
(ADR 0009): a real, working integration test that doesn't depend on
what happens to be installed where it runs.

## Not done (deliberately out of Phase 6's scope)

- Not wired into the GUI — no editor UI shows diagnostics, a completion
  popup, or jumps to a definition yet. That's real UX work (squiggly
  underlines, a popup widget, cross-file navigation) that belongs with
  Phase 7 polish or its own pass, not implied by "the client module
  works." Tracked in `docs/ROADMAP.md`, matching how Phase 5's plugin
  host also isn't wired to a keybinding yet.
- No `textDocument/didChange` — the client can open a document but
  can't tell the server about edits after that. Fine for a first
  request/response smoke test; a real editing session would need this
  before diagnostics/completion results reflect current buffer state.
- No workspace-wide features (symbols, rename, references) — sticking
  to exactly what section 7 asked for.

## Consequences

Everything above the framing layer (`process_complete_messages`) is
platform-independent and fully tested without needing a live process at
all beyond the fake server. Real language servers (clangd, pyright,
etc.) should work as-is for the three features implemented — this
hasn't been verified against one, only against the fake fixture and
the LSP spec's documented message shapes.
