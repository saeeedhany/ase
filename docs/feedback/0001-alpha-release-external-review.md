# Feedback 0001: Alpha release external review (Discord)

## Reporter

- **Name:** Ajeep
- **GitHub:** [`@TOTO-sys28`](https://github.com/TOTO-sys28)
- **Source:** [Discord](https://discord.com/channels/1547730271695282258/1547848976835936266)
- **Date:** 2026-09-11

Seven comments on the `v0.1.0-alpha` release, tried shortly after it
went up. Recorded here as one batch since they arrived together in one
message.

## Items

### 1. `.deb` requires Qt 6.11, Debian has Qt 6.10.2 — app doesn't start

> .deb requires Qt 6.11, while debian currently has Qt 6.10.2 → app doesn't start.

**Status:** Open

Confirmed, and it's a packaging mistake rather than a code bug: the
`.deb`'s `Depends:` line was generated with `ldd` + `dpkg -S` on the
machine that built it — a rolling-release Arch box with a newer Qt
than Debian stable ships. That bakes in whatever happens to be newest
on the build host instead of a real minimum version. Fix is to rebuild
the `.deb` inside an older, pinned base image (Debian stable itself,
or an equivalent Ubuntu LTS container) so the recorded dependency
floor is actually achievable on the distributions the package targets.

### 2. AppImage requires GLIBC 2.43 — doesn't start on older glibc systems

> AppImage requires GLIBC 2.43 → doesn't start on older glibc systems.

**Status:** Open

Same root cause as #1, worse consequence for this format specifically:
AppImages are conventionally built on an *old* base (Ubuntu
20.04/22.04-class) precisely so the glibc floor stays low — that's the
entire point of bundling an AppImage rather than just shipping a
binary. Building it on rolling-release Arch defeats that. Fix is the
same as #1: rebuild on an old, pinned base via `linuxdeploy` there
instead of on the dev machine.

### 3. CMake can finish successfully while silently skipping the GUI when Qt6 dev files are missing

> CMake can successfully finish while silently skipping the GUI when Qt6 development files are missing.

**Status:** Open

Confirmed at `gui/CMakeLists.txt:3-9`: a missing Qt6 produces
`message(WARNING ...)` and `return()`, not a build failure — so it's
not literally silent, but a warning that scrolls by in a long
configure log is easy to miss, and the result is a green build with no
GUI binary. Since `ASE_BUILD_GUI` defaults to `ON` and is something a
user can also pass explicitly, a missing Qt6 in that case should be a
hard `FATAL_ERROR` instead of a warning.

### 4. README has an outdated/inconsistent statement about undo/redo

> README has an outdated/inconsistent statement about undo/redo.

**Status:** Not a Bug

Checked the current `README.md` line by line: all three mentions of
undo/redo (Status section, architecture diagram, Layout section) are
consistent and describe it as implemented, with nothing hedged or
contradictory. This looks like it was true of an earlier version of
the README and has since been resolved as part of other edits. If a
specific stale line turns up, reopen this with the exact quote.

### 5. Keyboard shortcuts panel is cramped and requires internal scrolling; nested scrollbar feels awkward

> Keyboard shortcuts panel is cramped and requires internal scrolling; nested scrollbar feels awkward.

**Status:** Open

Confirmed at `gui/src/help_panel.cpp:101`: the panel is a fixed
480×420 single-column scroll area, which forces a nested scrollbar
once the shortcut list is long enough. Straightforward fix: widen the
panel and/or lay the shortcuts out in two columns so the full list
fits without an inner scrollbar.

### 6. Window control buttons look unfinished/placeholder-like

> Window control buttons look unfinished/placeholder-like.

**Status:** Not a Bug

Checked `gui/src/main.cpp`: ase doesn't draw its own title bar or
window control buttons at all — it's a plain `QMainWindow` with native
OS window decorations, and `FramelessWindowHint` doesn't appear
anywhere in the codebase. Whatever was seen is the reporter's own
window manager's theme, not ase's UI. Reopen if this refers to some
other button (e.g. inside a panel) rather than minimize/maximize/close.

### 7. Build emits a `tmpnam()` security warning

> Build emits a tmpnam() security warning.

**Status:** Open

Confirmed — it comes from vendored Lua (`_deps/lua-src/loslib.c`), not
ase's own code. `FetchContent` builds Lua without defining
`LUA_USE_POSIX`/`LUA_USE_LINUX`, so Lua falls back to its unsafe ISO
`tmpnam()` path instead of its own `mkstemp()`-based POSIX path. Since
the project is already POSIX-only for Lua, adding that compile
definition to the fetched target is a one-line fix with no downside.
