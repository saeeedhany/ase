# ADR 0025: Command line + `:compile` + output panel

## Status

Accepted

## Context

Phase 15 of the "complete normal editor" plan. Two design questions
were resolved with the user before implementing, both confirmed
exactly as proposed:

1. The `:` command line joins the `FloatingPanel` family established
   in ADR 0022 — same centered/flat/animated treatment as Find/Replace
   and Open/Save-As, since it's the same "glance, act, dismiss"
   interaction shape.
2. The output panel deliberately does **not** — it's docked at the
   bottom of the window instead. Find/Replace/Open/Save-As/the command
   line are all quick, modal, and fine to float centered over your
   code because you're not looking at the code while they're open. The
   output panel is different: you want to watch a build stream while
   still looking at, and jumping back into, your code. A centered
   overlay would fight that.

## Decision

### `AseProcess` (`core/include/ase/process.h`, `core/src/process.c`)

Extracted from the LSP client's existing (previously file-local,
duplicated-in-spirit-if-this-phase-hadn't-happened) `platform_spawn`/
`platform_read_nonblocking`/`platform_write_all`/`platform_terminate`
— same fork/exec/pipe/`SIGPIPE`-handling code, same POSIX-real/
Windows-stub split (ADR 0011 decision 6: `ase_process_spawn` returns
`NULL` cleanly on Windows rather than shipping untested async I/O).
Two additions beyond a straight extraction, both needed for
`:compile` specifically (the LSP client never needed either):
- **`cwd` parameter**: the child `chdir()`s before `execvp` (and
  refuses to run, `_exit(127)`, if that fails — running a build in the
  wrong directory is actively misleading, not a case to silently
  ignore).
- **stderr merged into the same stream as stdout** (`dup2` onto both
  fds) — compiler errors go to stderr; a build tool's output panel
  that only shows stdout would silently miss most of what you actually
  want to see, so v1 doesn't attempt to split them.
- **`ase_process_has_exited`/`ase_process_exit_code`**: a non-blocking
  `waitpid(..., WNOHANG)` check, cached once true. The LSP client
  never needed this (it tracks liveness via read EOF/error instead,
  its own `alive` flag) but `:compile` needs the real exit code to
  report.

`lsp_client.c` is now a consumer of this module — its struct holds one
`AseProcess *process` instead of `child_pid`/`read_fd`/`write_fd`
separately, and every call site updated accordingly. Verified this
refactor didn't change LSP behavior: the full existing
`ase_lsp_client_tests` suite (handshake through shutdown, against the
fake LSP server fixture) still passes unmodified. New
`core/tests/test_process.c` covers the module directly: stdout capture,
non-zero exit codes, `cwd`, a full write→read round trip through
`cat`, and the `execvp`-fails-in-the-child case (spawn still succeeds,
exit code 127).

### `CommandLine` (`gui/src/command_line.{h,cpp}`)

A `FloatingPanel` with one `":"` `LetterBadge` and one field — the
smallest member of this family, matching `FindBar`'s shape almost
exactly. `:` opens it (checked via the *produced character* in
`keyPressEvent`, not a keycode — `:` is a shifted key on most layouts,
and Qt has no layout-independent "colon" key). `Enter` reads the typed
text, closes the panel, and calls `EditorViewport::runCommand` — kept
in that order (close *before* running) so a command that itself
touches focus or opens another panel isn't fighting the command line's
own close animation. `Escape` cancels without running anything. No
history, no buttons, no tab-completion — v1 stays exactly as small as
`FindBar` was.

`EditorViewport::runCommand` dispatches on the trimmed text: `w` →
`save()`, `q` → `window()->close()`, `compile` → `compile()`, `output`
→ toggles the output panel's visibility. Anything else is a **silent
no-op** — matches the plugin host's existing "skip, don't crash"
tolerance (ADR 0009) rather than an error message for a typo.

### `:compile` (`EditorViewport::compile`/`pollCompile`)

Reads `build_command` from config **fresh on every call** (not
cached, same hot-reload spirit as everything else config-driven here)
— **no default value** (`ase_config_create_default` doesn't set it),
matching the config's own "don't guess" convention already established
for optional settings. Unconfigured, no open file, or a build already
running are each reported as a line in the output panel, not a crash
or a silent no-op — you asked for something to happen, so something
visible happens even when it's "can't do that, here's why."

`%f` is substituted for the current file's path; the process runs via
`/bin/sh -c "<substituted command>"`, not `execvp`'d directly — the
config key is documented (and the starter template's own example
shows) as a *shell* command, so `&&`/pipes/etc. need real shell
interpretation. `cwd` is the file's directory. `pollCompile()` is
driven by a `QTimer` (100ms, same non-blocking-poll shape
`ase_lsp_client_poll` already established), draining whatever's
available into the output panel each tick and appending an
`[exit code N]` line once `ase_process_has_exited` — at which point the
process is destroyed and the timer stopped.

### `OutputPanel` (`gui/src/output_panel.{h,cpp}`)

Deliberately **not** a `FloatingPanel` — see Context above. A plain
`QWidget` holding one read-only `QPlainTextEdit`, added as a real
`QVBoxLayout` row in `main.cpp` below `EditorViewport` (which now
needs a wrapper `QWidget` as the window's central widget again, this
time for a genuine layout reason rather than the one Phase 13 briefly
had and Phase 13.5 reverted). Starts hidden until the first
`:compile`; `:output` toggles it after. Themed with the editor's own
plain `background`/`text` colors, not the translucent `panel_background`
tone floating chrome uses — it's a docked extension of the window, not
an overlay, so it should read as part of the editor rather than
chrome sitting on top of it.

## Consequences

Verified live via `xdotool`: the command line opens centered with the
same look as `FindBar`; `:compile` against a real `build_command`
(`echo "compiling %f"; sleep 0.5; echo "done"`) streams the command
line, both `echo` outputs, and `[exit code 0]` into the output panel,
which appears docked below the editor text, correctly themed;
`:w` saves; `:output` toggles without incident. `core`'s test suite
grew by one file (`test_process.c`) and stayed green throughout the
`lsp_client.c` refactor (9/9 passing, up from 8/8 — the new suite, not
a regression fix).

Not built: stdout/stderr aren't separated (documented above, a
deliberate v1 simplification, not a half-built feature); no ANSI color
parsing in the output panel; no click-to-jump from a compiler error
line to that location in the buffer; no command history or tab-
completion in the command line. Vim mode is next per the roadmap, now
that Phase 15 — the last item before it — is done; it gets its own
dedicated planning pass, per the standing direction, rather than being
scoped here.
