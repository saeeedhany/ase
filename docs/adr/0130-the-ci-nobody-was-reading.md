# ADR 0130: The CI nobody was reading

## Status

Accepted

## Context

CI on `main` had never passed. Not one run since the first, on
2026-09-11 — through a hundred commits, two CI jobs added to it, and a
beta release cut and published with a CI badge in the README rendering
red the whole time.

Nobody looked, including while adding jobs to it. Every "the tests pass"
in that period was a local `ctest` run, which was true and is not the
same claim.

## Decision

Six failures, found by reading the logs in order and fixing what each
one actually said.

### The test that segfaulted on every Ubuntu run

`memmem()` is a GNU extension. Without `_GNU_SOURCE` it is not declared,
so C implicitly typed it as returning `int`, the returned pointer came
back truncated to 32 bits, and the `memcpy` through it landed on
nothing. It had been doing that since ADR 0110's commit.

Replaced with a five-line local search rather than reaching for
`_GNU_SOURCE`, because this file also has to build where `memmem` does
not exist. For the same reason it no longer uses `dirent.h`, `unistd.h`
or `utime.h`; cleanup asks `ase_recovery_list()` what is in the
directory, which is portable and exercises the API besides.

### The configure that failed before compiling anything

`set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ...)` ran
unconditionally, but that cache entry is only created under
`if(NOT CMAKE_CONFIGURATION_TYPES)` and Visual Studio is a multi-config
generator. Every Windows run died there.

It also took the macOS and Linux core jobs with it through `fail-fast`,
so those had never reported either. `fail-fast` is off: one platform's
failure hiding two others' results is the opposite of what the matrix is
for.

### The core that had never compiled where it claimed to

With configure fixed, Windows reached the compiler for the first time
and stopped on `<strings.h>`. The list was longer than one header:
`strcasecmp` (now `ase_strcasecmp`, `_stricmp` on Windows), `usleep`
(now `Sleep`), and `<strings.h>` in two more places.

### The failure that only ever happened on a runner

`ase_lsp_client_tests` aborted under ASan on `CHECK(client != NULL)`, and
reproduced on nothing here — not 22.04, not 24.04, not with sanitizers,
not under artificial load.

A diagnostic printing `errno` answered it in one run: **Broken pipe**.

A command that does not exist execs, fails, and `_exit()`s. If it loses
the race to the parent's `initialize` write, the pipe has no reader and
the write takes `EPIPE`; if it wins, the bytes sit in the pipe buffer and
the write succeeds. `ase_lsp_client_start()` tore the client down on one
side of that race and returned it on the other — so a misconfigured
`lsp_command` behaved differently depending on machine load.

`write_framed()` already sets `alive = false`, so the client was correctly
dead either way and `start()` was throwing it away. It is returned now,
and `poll()` reports the death, which is what
[ADR 0093](0093-the-handshake-is-not-awaited.md) says happens when the
handshake is not awaited.

The first version of that diagnostic printed to stdout and never reached
the log, because `CHECK` calls `abort()` and an abort discards whatever
is sitting in stdout's buffer. `stderr`, flushed.

### The three that still crash

Windows now builds the core and runs its suites. Three segfault:

| suite | dies at |
| --- | --- |
| `ase_config_tests` | inside `ase_config_create_default()` |
| `ase_theme_tests` | reading a static table it has just been handed a valid pointer into |
| `ase_plugin_host_tests` | immediately after `register_command` returns |

That much is known because a crash prints nothing, so `RUN()` now names
each case on stderr before running it and the last name in the log is the
one that died. Struct packing, `stdbool` layout and the obvious ABI
mismatches were checked and ruled out.

The job keeps running and does not gate. Deleting it would throw away the
only signal anyone has about Windows; gating on it would block work on
the platform this editor actually ships for. `README.md` and the install
guide now say plainly that Windows is unverified — the previous wording,
"the core builds and runs on Linux, macOS and Windows in CI", was untrue
and was part of why nobody looked.

## Consequences

`RUN()` is worth keeping regardless of Windows: any suite that crashes
now says which case it was in, on every platform.

The LSP fix is a real behaviour change on Linux too, and it is the kind
that only a busy machine would ever have shown. It is the one defect here
that a green CI would have caught years of local runs could not.

What this cost: CI was added, extended twice, and never read. A red badge
on the front page of the repository is a signal that decays to zero the
moment it is normal, and it had been normal since the first run.
