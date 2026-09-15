# ADR 0094: What startup actually costs

## Status

Accepted

## Context

Startup had been measured only end-to-end — 81 ms for a small file, 203 ms
for a 277 KB one — which is enough to know whether it is acceptable and
nothing at all about where the time goes. This ADR records the breakdown,
because the useful result of measuring is usually the shape rather than
the total.

## Decision

### The measurement

Timestamps at each phase, offscreen, with a bare Qt6 widgets app built
alongside as the floor:

| phase | 11 KB | 277 KB |
|---|---|---|
| `QApplication` | 1.7 ms | 1.6 ms |
| read the file | 0.3 ms | 0.4 ms |
| `MainWindow` | 19 ms | 15 ms |
| `addBuffer` | 19 ms | 113 ms |
| `show` + event loop | 0.7 ms | 0.7 ms |
| **total** | **43 ms** | **98 ms** |

A bare `QMainWindow` with a status bar reaches its event loop in
**13.7 ms** on this machine. That is the floor nothing can go below, and
it means our own `MainWindow` costs about 7 ms — small enough not to be
worth attacking.

Reading the file is 0.3 ms. The buffer is not the problem and never was.

### What scales is the first parse

Inside `addBuffer`, for the 277 KB file:

| | |
|---|---|
| `loadConfig` | 1.0 ms |
| `rebuildSyntax` | 3.0 ms |
| **`refreshCache`** | **97 ms** |
| timers, panels | 0.2 ms |

`refreshCache`'s cost is the initial Tree-sitter parse. It is not
incremental — there is no previous tree to be incremental from — and
Tree-sitter has no way to parse only the visible range, so the whole file
is parsed before anything is drawn. 97 ms for 277 KB is simply what that
costs; the equivalent for a 60 KB file is around 20 ms, and for 11 KB
under 4 ms.

Deferring it so the window appears first was considered and not done: it
trades a slower startup for a visible flash of uncoloured text, and at
the sizes people actually edit the parse is already invisible.

### `AboutPanel` was built for every buffer

Each buffer gets its own set of floating panels
([ADR 0022](0022-floating-panel-design-system.md)), built eagerly in
`addBuffer`. Timed individually, all of them are free — `FindBar`
0.08 ms, `FileBrowserPanel` 0.08 ms, `HelpPanel` 0.19 ms,
`CompletionPopup` 0.04 ms, `HoverPanel` 0.02 ms — except one:

**`AboutPanel`: 8.05 ms.** It decodes `:/ase.png` and smooth-scales it,
in its constructor, for a panel reached by Alt+I and opened approximately
never. That cost was paid on startup *and* on every `Ctrl+N`, every file
opened, every new tab.

It is now built on first Alt+I. Every call site already null-checked the
pointer, so nothing else changed, and `openAbout()` calls `refreshTheme()`
itself — a lazily built panel is themed correctly without the config
reload a pre-built one would have received.

The other panels stay eager. 0.02 ms is not worth a lifetime rule.

## Consequences

Startup, median of five, after this and
[ADR 0093](0093-the-handshake-is-not-awaited.md):

| file | before | after |
|---|---|---|
| 11 KB | 81 ms | 81 ms |
| 60 KB | 112 ms | 101 ms |
| 277 KB | 203 ms | 121 ms |

Most of the large-file gain is ADR 0093 — startup no longer waits for
the language server handshake. The `AboutPanel` saving is real but 8 ms,
which this end-to-end method cannot resolve; it was measured directly
instead.

The first Alt+I now pays 8 ms to build the panel. It opens with an
animation that is longer than that, and it happens once per buffer.

What is left is mostly Qt's own 14 ms and, for large files, a parse that
has to happen. Neither is a bug, and saying so is the point of writing
the numbers down: the next person to wonder whether startup can be made
faster can read this instead of re-deriving it.
