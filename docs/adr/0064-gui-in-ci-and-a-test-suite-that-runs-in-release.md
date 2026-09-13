# ADR 0064: The GUI in CI, and a test suite that actually runs in Release

## Status

Accepted

## Context

`.github/workflows/ci.yml` built `-DASE_BUILD_GUI=OFF` on every job,
with a `# TODO: add a gui job once Phase 2 lands` comment left over from
Phase 1. Phase 2 landed a long time ago.

The cost of that gap is on the record: a Qt 6.6-only call
(`QList::assign`) reached `main` with ADR 0053 and stayed there through
several rounds of work, with **both packages unbuildable the whole
time**, because every build in between was native on a rolling-release
machine with Qt 6.10 and nothing ever compiled the GUI anywhere else. It
was found by a human cutting a release (addendum to ADR 0045), which is
the worst possible moment.

## Decision

### A GUI job, on the oldest Qt this project ships against

`ubuntu-22.04` is not an arbitrary runner choice: it is the AppImage's
own build base (packaging/README.md), and it ships **Qt 6.2** — the
floor `gui/CMakeLists.txt` now asks for explicitly. `debian:bookworm`'s
6.4 sits between that and whatever a dev machine happens to have. If it
compiles there, it compiles for everyone this project claims to support.

### The job launches the binary, not just links it

Linking proves the API exists. It does not prove the app starts: a
missing platform plugin, a bad resource path, or a constructor that
throws all link perfectly and then die on launch. So the job runs
`ase_gui` headless under `QT_QPA_PLATFORM=offscreen` and requires it to
*still be running* when a timeout fires — `timeout` returning 124 (had
to kill it) or 143 (took the SIGTERM) is the pass condition, anything
else means it exited on its own, which it should never do.

### The thing this immediately found: the test suite was vacuous in Release

The new job builds `Release`, because Release is what the packages ship.
It failed instantly — three suites **segfaulting**: `ase_buffer_tests`,
`ase_config_tests`, `ase_undo_tests`. That reproduced on the dev machine
too, so it was never a CI or container artefact.

The cause: every test used `assert()`, and CMake defines `NDEBUG` for
Release builds, which compiles `assert()` away entirely.

- **263 checks across nine test binaries evaluated to nothing.**
- **122 of them wrapped a call with side effects** —
  `assert(ase_undo_undo(undo, buf, &cursors, &count))` being the shape.
  Under `NDEBUG` the call itself vanished, the out-parameters it was
  meant to fill stayed uninitialised, and the next line freed or read
  them. Hence the segfaults; `ase_config_tests` was crashing *inside
  `fread()`* on a file that a deleted call had never written.

The three that crashed were the lucky ones. The other six passed while
testing nothing at all.

Every check is now `CHECK()` (`core/tests/test_assert.h`): always
evaluated, in every build type, reporting file, line and the failing
expression. A test that silently stops testing in the configuration you
ship is worse than no test, because it still reports success.

Nothing else in the project used `assert()` — no production code relied
on it for invariants — so this is confined to the test suite.

### Verified by breaking something on purpose

A fix that only stops the crash would be worse than useless, so the fix
was checked by mutation: an expectation in `test_undo.c` was changed to
a deliberately wrong value, and the Release build reported

```
core/tests/test_undo.c:11: CHECK failed: ase_buffer_length(buf) == len
```

Before this change, that same mutation passed silently in Release.

## Consequences

The whole job was run end to end in a bare `ubuntu:22.04` container
before being committed, since a workflow that has never executed is a
guess: Qt 6.2.4, GUI builds clean, **9/9 tests pass in Release**, and
the headless launch stays up. Both Release and Debug pass on the dev
machine too.

`build-essential` and `cmake` are named in the install step even though
the GitHub runner image already has them — that is what makes the job
runnable in a plain container, which is how it gets tested before it is
trusted.

Not done, and worth knowing:

- **No build caching.** Lua and Tree-sitter come down through
  `FetchContent` at configure time on every run, costing a couple of
  minutes. Worth an `actions/cache` entry when the wait starts to
  matter, not before.
- **No GUI job on macOS or Windows.** The process and LSP layers are
  POSIX-only by design (ADR 0011), and the core matrix already covers
  all three platforms headless.
- **Nothing runs the packaging containers in CI.** A release still means
  a human running `packaging/`'s Docker lines by hand. This job covers
  the compile-against-old-Qt half, which is the half that actually
  broke.
