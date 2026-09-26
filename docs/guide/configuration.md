# Configuration

One file, `key = value`, one per line. `#` starts a comment.

```ini
font_family = JetBrains Mono
font_size = 12
theme = simple-nord
vim_mode = true
line_numbers = relative
```

`:config` opens it. Every key is listed in
[Every config key](../reference/config.md).

## Where it is

| | |
| --- | --- |
| Linux, BSD | `$XDG_CONFIG_HOME/ase/config.ase`, i.e. `~/.config/ase/config.ase` |
| Windows | `%APPDATA%\ase\config.ase` |

It is written for you the first time ase starts, with every key commented
and explained. Deleting it and restarting gives you a fresh one.

## It reloads while you watch

Save the file and the editor picks it up — colours, font, line numbers,
everything. There is no reload command and no restart.

That is also true of a mistake: a key the editor does not know, or a
binding pointing at a command that does not exist, is reported in the
status bar as soon as you save. A typo that silently did nothing was
indistinguishable from a broken feature, so it says so instead.

## Per-project settings

A `.ase.conf` beside your code applies to that project only. The nearest
one at or above the file wins.

It may set **only** `filetype.*` — what a suffix means:

```ini
# .ase.conf in a C++ project that uses .h headers
filetype.h = cpp
```

It may not set `build_command`, `lsp_command`, or anything else that
names a program to run. A repository you cloned writes this file, and
opening a file in it should not be able to execute something. Anything
else in it is ignored, and the editor tells you it was.

See [ADR 0087](../adr/0087-project-config-says-what-files-mean.md).

## Colours

Nine colour keys, `#RRGGBB` or `#RRGGBBAA`. You rarely want to set them
by hand — pick a [theme](themes.md) instead, which sets all nine.

If you *have* set one by hand, a theme leaves it alone. A value you
changed is a decision; a value still equal to what shipped is not, and a
theme may replace it.

## Large files

Syntax highlighting is parsed on its own thread, so a large file opens
and edits without waiting for it — the colours arrive when the parse
finishes and the text is plain until then.

`syntax_max_kb` (4096 by default) is the point past which a file gets no
highlighting at all. That bounds **memory**, not responsiveness: a parsed
buffer costs roughly 26 times its source size. Raise it if you routinely
open very large files and have the memory for it.

## Languages and servers

```ini
lsp_command = clangd              # any language without its own
lang.python.lsp = pylsp           # one language
lang.rust.lsp = rust-analyzer
filetype.h = cpp                  # what a suffix means
```

See [Language servers](lsp.md).

## Build command

```ini
build_command = cmake --build build
```

Leave it unset and `:compile` works out what to run instead: a
`compile_commands.json` entry for the file you are in, or the outermost
`CMakeLists.txt`, `Makefile`, `Cargo.toml`, `go.mod`, `meson.build` or
`package.json` between here and the top of the repository. It shows what
it found and puts the command in the `:` line — press Enter to run it,
or edit it first. It never runs a command you have not seen.

Errors and warnings from the build are marked in the gutter and
underlined on the lines that produced them, the same way a language
server's are. They stay until the next build.

`:compile` or `Alt+B` runs it and puts the output in a panel below the
buffer. `%f` in the command is replaced with the current file, so
`gcc %f -o /tmp/a.out` works on whatever you are looking at.
