# ADR 0063: Language-server state in the status bar

## Status

Accepted

## Context

An external tester reported that LSP "doesn't work" in the AppImage and
`.deb` builds. It wasn't a packaging bug. `lsp_command` ships commented
out in the default config (`core/src/config.c`), and
`startLspClientIfConfigured()` returned silently when it was unset — so
a C file opened with no diagnostics, no completion, no hover, and
**nothing anywhere saying why**. Three quite different situations —
not configured, configured but the binary isn't there, working fine —
were indistinguishable from each other and from a broken editor.

ADR 0062 built the message line and deliberately stopped short of this:
a message fires once and is gone, so it cannot answer a question asked
ten minutes later. This is state.

## Decision

### A segment, sitting left of the cursor position

Five states, one label:

| state | shows | when |
|---|---|---|
| `NotApplicable` | *nothing* | a file no server would be started for |
| `Unconfigured` | `no lsp` | `lsp_command` not set |
| `Running` | `clangd` | the handshake completed |
| `Failed` | `clangd failed` | configured, but it never came up |
| `Stopped` | `clangd stopped` | it came up, then went away |

The running state **names the server rather than announcing it**:
`clangd` sitting two opacity tiers down beside the cursor position
answers "is one running?" without ever competing with the file for
attention. It is reference information you go looking for, and it should
be findable and otherwise invisible.

The failure states are the only ones in the theme's `diagnostic_error`
colour, because they are the only ones you might act on.

`no lsp` is deliberately quiet but *present*. Every packaged install
starts in that state, and a blank status bar is exactly what made it
read as broken. Showing nothing at all for files no server would handle
(a `.txt`, a `.md`) is the other half of the same judgement: an
indicator that is permanently blank for most files is noise with extra
steps.

### It follows the buffer, not the window

Each buffer starts its own server (ADR 0054), so the segment shows the
state of the file you are looking at, exactly like the title and the
position readout. Switching tabs repaints it from
`EditorViewport::lspState()` rather than waiting for a transition that
may never come.

### Death is noticed, not assumed

A server that comes up and later dies — a crash, an OOM kill, someone's
`pkill` — used to leave the editor reporting a language server that
isn't there, which is worse than reporting none. `checkLspAlive()` runs
on the existing 750ms config-reload timer rather than a second timer of
its own: both ask "has something outside this process changed under
us?", and one timer is one thing to reason about.

### Two layers, not the same sentence twice

The first cut had the transient message and the permanent segment saying
identical words at the same moment, which reads as a duplicate rather
than as two layers. They now divide the job:

- the **message** says what to *do* — `clangd not found — check
  lsp_command`
- the **segment** says what *is* — `clangd failed`

The message catches you at the moment it happens; the segment answers
"why are there no diagnostics?" an hour later. Neither is sufficient
alone, which is the whole argument for having both.

Nothing is said at all for `Unconfigured`: an error message on every C
file you open, for a state that is normal and deliberate, is nagging.
The quiet segment carries it.

## Consequences

Verified live, all five states:

- A `.c` file with `lsp_command = clangd`: `clangd`, dim, next to the
  position readout.
- `pkill`ing that clangd: within a second, `clangd stopped` in red, plus
  a one-time message.
- A config with no `lsp_command`: `no lsp`.
- `lsp_command = clangd-does-not-exist`: `clangd-does-not-exist failed`
  in the segment, `clangd-does-not-exist not found — check lsp_command`
  on the message line.
- A `.txt` file: nothing at all.

**A real bug in ADR 0062 fell out of this.** Messages were elided once,
at post time, against the status bar's width — but the first message of
a session is posted *while the window is still being built* (the server
starts on the first buffer's activation, before `show()`), when that
width is a meaningless default. Early messages were being cut to a few
characters on a window with plenty of room: `clangd…ig.ase`. The full
text is now kept and re-elided on every resize, which also fixes the
case nobody had tested — making the window narrower with a message up.

`ctest` 9/9, clean build, zero warnings.

Still open, and now the obvious next step on this thread: `lsp_command`
ships commented out, so the good path requires editing a config file
you don't know exists. The segment makes that discoverable rather than
invisible; it does not make it unnecessary.
