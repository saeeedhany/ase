# ADR 0096: One server per language and project, not per buffer

## Status

Accepted

## Context

Every `EditorViewport` owned an `AseLspClient`. Open four files and four
language servers started, each spawning its own process, building its
own preamble, and indexing the same project independently.

`onActivated()` said so in a comment — *"One project-wide server is the
real fix for N files meaning N servers"* — and
[ADR 0029](0029-lsp-diagnostics-wiring.md) had left it as known future work.
What was missing was the size of it.

Measured, three buffers open (one C, two C++) against this repository:

| | processes | total RSS |
|---|---|---|
| before | 4 | **1601 MB** |

The editor itself was 43 MB. On a real C++ project, where a single
clangd is routinely 500 MB to 1 GB, six open files is an
out-of-memory condition rather than an inefficiency.

A second thing was wrong and invisible: `ase_lsp_client_start(argv,
nullptr)` — the server was never told the project root. clangd then has
no anchor for `compile_commands.json` discovery or cross-file indexing.

## Decision

### The registry owns servers; buffers borrow them

`LspRegistry` keys a client by **(language, project root)** and hands the
same one to every buffer that asks. It lives on the window and is passed
to each viewport — the shape `OutputPanel` and `CommandLine` already use
for "there is one of these".

Root comes from `project::rootFor()`, the same walk `Ctrl+P` uses, so
"project" means one thing in the editor rather than two.

Measured after, same three buffers:

| | processes | total RSS |
|---|---|---|
| before | 4 | 1601 MB |
| **after** | **2** | **919 MB** |

Two, because C and C++ are different languages and get different
servers. That is correct, not a shortfall — and it no longer grows with
the number of files.

### Servers outlive buffers, so documents must be closed

A per-buffer client could be discarded whole. A shared one cannot: when
a buffer closes, its *document* has to go without the server going with
it. `ase_lsp_client_did_close` is new for exactly this, and the registry
refcounts users — the last buffer to let go is the one that stops the
server.

Verified by tracing the lifecycle: `start` (1 user) → `reuse` (2 users,
still one server) → `keep` on closing one → `stop` on closing the last.

### The registry polls, and owns the state

One 200 ms timer for all servers, where each buffer used to bring its
own. Because state now belongs to the server rather than the buffer, the
registry drives it and pushes changes to every buffer using it: a buffer
joining a server that is already up is told immediately, and a server
that dies reports to all of its buffers at once.

Diagnostics dispatch to every user of a server; each filters by URI, as
it already did. That filter stopped being defensive and became the
routing.

### `QPointer`, not a raw pointer

The registry and the viewports are both children of the window, and
Qt's teardown order is not ours to assume. A raw pointer would dangle
rather than null, and the viewport's destructor calls into it — a crash
on exit, found by reasoning about ownership rather than by it happening.

## Consequences

**Memory no longer scales with open files.** Ten C++ buffers now cost
one server instead of ten. The remaining two servers are the price of
supporting two languages at once.

**clangd now gets a `rootUri`**, so `compile_commands.json` discovery
and cross-file navigation work from the project root rather than from
whatever directory a file happens to sit in.

Buffers in *different* projects still get different servers, which is
the point of keying on root: two checkouts of the same repository must
not share an index.

A server is now shared state. A crash takes diagnostics away from every
buffer using it at once, where before it affected one — they are all
told, and the status bar shows `stopped` for each. Restarting a dead
server is still not implemented, in either design.

`checkLspAlive()` and the per-viewport `pollLsp()` are gone, along with
the per-buffer poll timer. The 750 ms config timer no longer carries a
liveness check it was only sharing a beat with.
