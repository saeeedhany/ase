# ADR 0147: Working out how to build

## Status

Accepted

## Context

`:compile` ran `build_command` from `config.ase`, and without one it
said "No build_command configured" and stopped. So the build key did
nothing at all until you had configured it per project, which is the
sort of thing people configure once, in one project, and never again.

ROADMAP's Tier 3 proposed inferring it, with the constraint attached:

> **Show the inferred command for confirmation rather than silently
> running it** — this project's standing rule is that an unconfigured
> thing is reported, never guessed (ADR 0029), and a misdetected build
> target running silently would violate that badly.

## Decision

`ase_build_infer()` in core finds *a* way to build and never runs it.
The GUI shows what it found, says which file decided it, and puts the
command in the `:` line with the caret after it. Enter runs it, any key
edits it first, Escape drops it.

`:compile <command>` is now a thing on its own, which is what the offer
comes back as and is also useful for a one-off build.

### What it looks for

A `compile_commands.json` entry for the file you are in wins, and takes
the *nearest* one. It is not a guess about the project — it is the build
system's own record of how this exact file is compiled. Both shapes in
the spec are read: cmake writes `command`, other tools write
`arguments`.

Otherwise the first of these, per directory:

| marker | command |
| --- | --- |
| `build/CMakeCache.txt` | `cmake --build build` |
| `build.ninja` | `ninja` |
| `CMakeLists.txt` | `cmake -B build && cmake --build build` |
| `Makefile` | `make` |
| `Cargo.toml` | `cargo build` |
| `go.mod` | `go build ./...` |
| `meson.build` | `meson compile -C build` |
| `package.json` | `npm run build` |

A configured build directory beats the file that would configure one,
because running `cmake -B` over an existing build is the slow answer to
the same question.

### The outermost marker, not the nearest

This is the part that was wrong first, and the editor's own source is
what showed it. Opening `core/src/build.c` and pressing the key offered
`cmake -B build && cmake --build build` **in `core/`** — it had found
`core/CMakeLists.txt`, which describes a component, and would have
configured a second build tree inside it.

A marker in a subdirectory is usually part of something larger. So the
walk collects candidates all the way up and the outermost wins.

That needs a bound, or the walk reaches a home directory with somebody
else's `Makefile` in it. A directory holding `.git` stops it, which is
what people mean by "this project" nearly always.

Two smaller things the same run exposed:

**The path has to be absolute.** The walk goes up by trimming
components, so `core/src/build.c` runs out at `core` and never reaches
the root — which is exactly why the first fix appeared not to work.

**`:compile` ran its output into the echoed command.** `$ makebuilding
the thing`, because `appendLine()` leaves the cursor at the end of the
line it wrote and process output arrives through `appendText()`, which
inserts raw. It predates this change and was invisible while `:compile`
needed configuring first.

### What it deliberately does not do

It does not fall back to something plausible. An unrecognised tree is
reported as unrecognised, which is ADR 0029's rule and the reason the
marker list is short and boring: every command is what the tool's own
documentation tells you to type.

It also does not remember. Confirming a command does not write it into
`config.ase` — the output panel says to put it there, and that stays a
thing a person does on purpose.

## Consequences

Ten tests over the inference, which is pure: given a tree, what command.
They cover the nearest/outermost distinction, the `.git` bound with no
`stop_at` at all, a configured build directory beating a bare
`CMakeLists.txt`, both `compile_commands.json` shapes, a database that
does not mention this file falling through to the marker, and a
malformed one not being fatal.

Verified in the editor twice, and the second time is the point: on a
throwaway `Makefile` project it offered `make` and ran it, and on this
repository it offered `cmake -B build && cmake --build build` in the
wrong directory. After the fix it offers `cmake --build build` from the
repository root, which is the command this project is actually built
with.

What is still open from the ROADMAP entry: nothing reads the build's
output back into diagnostics, so errors are text in a panel rather than
marks in the gutter. That is a separate feature and a bigger one.
