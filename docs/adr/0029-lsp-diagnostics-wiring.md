# ADR 0029: LSP diagnostics wiring (Phase 16)

## Status

Accepted

## Context

The LSP client (ADR 0011) could start a server, send `didOpen`, and
receive diagnostics once — but had no way to refresh them after an
edit, and the GUI never rendered whatever diagnostics it got. Phase 16
closes both gaps: a real "diagnostics as you type" loop, end to end.

## Decision

### `textDocument/didChange` — full-document sync

`ase_lsp_client_did_change(client, uri, version, text)` sends a
`textDocument/didChange` notification with one `contentChanges` entry
containing the whole new `text` and no `range` field
(`TextDocumentSyncKind.Full`), not an incremental diff — consistent
with this project's existing full-buffer-mirroring approach elsewhere
(ADR 0006) rather than tracking edit ranges. Called from
`EditorViewport::refreshCache()`, the one choke point every edit
already passes through, so diagnostics never go stale after the first
keystroke — the gap this phase exists to close. `fake_lsp_server.c`
was extended to reply to `didChange` with an *empty* diagnostics list
(didOpen's canned reply has one), specifically so the new core test
can prove the notification actually reached the server and produced a
fresh callback, not just that some stale callback fired again.

### New config: `lsp_command`, `diagnostic_error`, `diagnostic_warning`

`lsp_command` follows `build_command`'s exact "no default, don't
guess" pattern (ADR 0025) — unconfigured means the LSP feature is
simply off, not a guess at which server the user has installed.

`diagnostic_error`/`diagnostic_warning` are new, real (non-empty)
default colors — a deliberate, documented departure from the "one
font color" pillar: error/warning color-coding is too strong and too
widely expected a convention to fold into the opacity/weight variation
syntax highlighting uses instead.

### GUI lifecycle: tied to the open file, not the app

`EditorViewport::startLspClientIfConfigured()` reuses the same `.c`/
`.h` suffix gate Tree-sitter highlighting already uses, and is called
from both the constructor and `openFile()` (which first stops any
previous client and clears `m_diagnostics`, mirroring the
constructor's own startup order) — so switching files always talks to
a fresh server against the right document, never a stale one. Polled
non-blockingly every 200ms (`m_lspPollTimer`), faster than config-
reload's 750ms and comparable to `:compile`'s 100ms output poll, as a
reasonable balance for diagnostic responsiveness without busy-waiting.

`GuiDiagnostic` is a necessary GUI-side copy of `AseLspDiagnostic`:
the core struct's `message` is a borrowed pointer, valid only during
the synchronous callback, so it can't be stored as-is across frames.

### Rendering: squiggle + gutter dot

`drawSquiggle()` (a small zigzag `QPainterPath`, same per-line
splitting as the existing `highlightRange()`) draws a wavy underline
under each diagnostic's range, colored by severity
(`colorForSeverity()`: 1=error, 2=warning, anything else falls back to
a dimmed text color). A small filled-circle "gutter dot" is drawn per
line in the left padding the right-aligned line-number text never
reaches — worst (lowest-numbered) severity among any diagnostic
spanning that line wins.

LSP `character` is treated as a direct byte offset within the line —
an ASCII-only v1 simplification, consistent with this codebase's
existing byte-level cursor shortcuts (ADR 0012). `offsetForLineColumn`
clamps out-of-range lines, so a diagnostic that's gone stale mid-edit
(referring to a line that no longer exists) degrades harmlessly rather
than drawing garbage or crashing.

`applyLspDiagnostics` (the callback target) had to move from private
to public, since the C-callback trampoline bridging `AseLspClient`'s
function-pointer callback to the C++ object is a free function in an
anonymous namespace, not a member function, and can't be made a
friend of an unnamed-namespace function across the header/.cpp
boundary cleanly. Documented in place as "public only for the
trampoline."

### Real bug found and fixed: merged stderr corrupted the protocol stream

`AseProcess` (ADR 0011/0025's shared process-spawn module) always
merged a child's stderr into the same pipe as its stdout, on the
assumption — true for `:compile` and for the black-box test fixture —
that either the merge is desired (build output) or the child never
writes to stderr at all. A **real** LSP server breaks that assumption:
`clangd` logs routinely to stderr, and interleaving that into the
Content-Length-framed JSON-RPC stdout stream corrupts the framing,
silently breaking every response after the first stray log line. This
was invisible against the test fixture (which never touches stderr)
and only surfaced live against real `clangd` — `ase_lsp_client_start`
was returning `NULL` with no diagnosable reason until traced with
temporary debug logging.

Fixed by splitting `ase_process_spawn` into a thin wrapper over a new
`ase_process_spawn_ex(command, cwd, merge_stderr)`, which only `dup2`s
the child's stderr onto the stdout pipe when `merge_stderr` is true.
`:compile` keeps calling the original `ase_process_spawn` (merge
still true, unchanged behavior). The LSP client now calls
`ase_process_spawn_ex(command, NULL, false)` — the child's stderr is
left inherited (not piped, not discarded), so server logging is still
visible wherever the caller's own stderr goes, but never touches the
protocol stream. A new core test, `test_stderr_merge_toggle`, spawns a
shell that writes to both streams and asserts both directions: merged
mode still contains both, separated mode contains only stdout.

## Consequences

All 9 ctest suites pass, including the new `ase_process_tests`
stderr-toggle case and the new `ase_lsp_client_tests` didChange
round-trip. Verified live against the real `clangd` binary
(`/usr/bin/clangd`), not just the fake fixture: opened a `.c` file
with a missing semicolon, confirmed a red squiggle + gutter dot
appeared on the affected line within about a second; retyped the file
with the fix in place (via `xdotool`, simulating real keystrokes) and
confirmed the squiggle and dot disappeared without restarting the
editor — direct proof `didChange` refreshes diagnostics, the specific
gap this phase exists to close.

Not attempted: incremental (range-based) sync — full-document sync is
simpler and this codebase already mirrors the whole buffer elsewhere,
so the extra complexity isn't justified yet. Also not attempted:
surfacing diagnostic `message` text anywhere (e.g. a hover/tooltip) —
v1 is visual-only (squiggle + dot); `GuiDiagnostic.message` is stored
and ready for that follow-up. Completion and go-to-definition
(`ase_lsp_client_request_completion`/`_request_definition`) remain
implemented at the core layer only, with no GUI wiring — flagged as
the natural next LSP-related phase.
