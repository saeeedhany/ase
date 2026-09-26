# ADR 0143: The header Windows never got

## Status

Accepted

## Context

Three core suites had segfaulted on Windows for long enough that the CI
job was named "known broken, not gating" and carried
`continue-on-error: true`. ROADMAP recorded the three and noted that
none reproduced on Linux or macOS, with or without sanitizers.

A job that always fails is a job nobody reads. It had stopped being a
signal.

## Decision

Fix it, and make the job gate.

### One cause, and it was not a Windows bug

`config.c` and `plugin_host.c` both had `#include "internal.h"` inside
the `#else` of an `#if defined(_WIN32)`. On Windows the declaration of
`ase_strdup()` never arrived, so C declared it implicitly as returning
`int`, and the returned `char *` was truncated to 32 bits.
`config_set_from()` stored the truncated pointer and the next
`strcmp()` over it crashed.

That accounts for all three:

- `ase_config_tests` died in its first test, which calls
  `ase_config_create_default()`.
- `ase_plugin_host_tests` died in the first test that registers a
  command — the one line in that file that copies a name.
- `ase_theme_tests` died reading a theme file, which goes through config
  to get there. `theme.c`'s own include was always fine, which is why
  the earlier guess at a static-table problem went nowhere.

This is the same shape as `memmem` in
[ADR 0130](0130-the-ci-nobody-was-reading.md) — also a pointer truncated
to 32 bits by an implicit declaration, also caught only by a platform
nobody develops on. Twice is a pattern, so an implicit declaration is
now an error rather than a warning: `/we4013` on MSVC,
`-Werror=implicit-function-declaration` elsewhere, both scoped to C
because neither applies to C++. Removing the include again fails the
build here, with `initialization of 'char *' from 'int'` — the
truncation itself, named, where it costs nothing to find.

### Then it hung, then it did not compile

Each fix uncovered what had been hiding behind the crash. Worth
recording because the sequence is the argument for fixing a broken job
rather than documenting it:

**The job stopped finishing.** Twenty minutes on a Test step that used
to take three seconds. `abort()` under the Debug CRT opens a modal
"Debug Error!" box and a crash opens Windows Error Reporting; nobody
clicks either on a runner. The suites now got far enough to fail an
assertion, and the failure hung instead of reporting. `RUN()` turns both
off before each case, and ctest runs with `--timeout 120` so a hang is a
failed test rather than a burned job.

**Then the build broke, and that one was self-inflicted.** Turning the
dialogs off needs `<windows.h>`, which defines `small` as a macro.
`test_assert.h` is included by every suite and `test_keymap.c` has a
`char small[8]`, which became `char char[8]`. `WIN32_LEAN_AND_MEAN`, and
`small`, `near` and `far` undefined after the include.

### Native plugins had never worked on Windows

With the crash gone, eleven of twelve suites passed and
`ase_plugin_host_tests` failed cleanly on `loaded == 2` — the first time
that test had ever *run* there, because the crash was three tests
earlier. Two independent reasons, either of which alone would have been
enough:

**The fixtures were in the wrong directory.** A multi-config generator
appends the configuration, so they were built into
`plugin_fixtures/Debug/` while the test looked in `plugin_fixtures/`.
The loader printed nothing because it never saw a `.dll`. The
`$<1:...>` wrapper is the documented way to say "this directory,
exactly".

**And nothing in them was exported.** A Windows DLL exports no symbols
unless it says so, so `GetProcAddress` would have come back empty for
both `ase_plugin_register` and the ABI version symbol even once the
files were found. `ASE_PLUGIN_EXPORT` is `__declspec(dllexport)` there
and empty everywhere else, so a plugin author writes it either way.

## Consequences

**12 of 12 core suites pass on Windows**, and the job gates: no
`continue-on-error`, and the name no longer says "known broken".

Three of the four entries this leaves on ROADMAP's follow-up list were
one bug and two consequences of never having run the code. What looked
like a platform problem was a missing `#include` that two other
platforms forgave.

The lesson [ADR 0130](0130-the-ci-nobody-was-reading.md) already drew,
paid again: a red job teaches nothing. The reason this took one evening
rather than a day of guessing is that the suites print each case's name
before running it, so a crash log still says which test died — which is
the one thing that ADR's Windows section did leave behind.

Still not addressed: `ase_process_spawn()` returns NULL on Windows, so
the LSP client and `:compile` are present and do nothing there. That is
real work, not a missing header.
