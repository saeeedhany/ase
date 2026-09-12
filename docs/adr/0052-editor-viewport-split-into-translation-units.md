# ADR 0052: Splitting EditorViewport across several translation units

## Status

Accepted

## Context

`gui/src/editor_viewport.cpp` had grown to **3608 lines — 51% of the
entire GUI layer** in a single file, with a 724-line header alongside
it. It accumulated that way honestly: every phase since ADR 0006 added
its feature to the one widget that owns the cursor model, the buffer
cache, and the paint loop, and each addition was individually
reasonable. By the Vim-mode work (ADR 0046–0051) the file held eight
distinct concerns — config/theme, painting, animation, the
cursor/selection model, Vim's modal engine, find/replace, file and
process commands, and the whole LSP surface (diagnostics, completion,
hover).

The seams were already visible in the file itself: it carried its own
hand-written section dividers (`/* --- Vim mode */`,
`/* --- completion */`, `/* --- hover */`) exactly where the
boundaries belonged.

The core library is not the problem and was left alone — its largest
file is `json.c` at 832 lines and each has one clear job.

## Decision

### Split the .cpp, not the class

`EditorViewport` is now defined across nine translation units, all
implementing the same class:

| File | Lines | Holds |
|---|---|---|
| `editor_viewport.cpp` | 169 | construction/teardown, `refreshCache`, offset↔line/column helpers, `isModalPanelOpen` |
| `editor_viewport_config.cpp` | 188 | config load/apply/hot-reload, font rebuild + runtime zoom |
| `editor_viewport_render.cpp` | 877 | `paintEvent`, the animation step, all drawing/measurement, diagnostic presentation |
| `editor_viewport_input.cpp` | 426 | key/wheel/mouse/leave/focus event handlers |
| `editor_viewport_edit.cpp` | 584 | cursor + selection model, insert/delete, clipboard, undo/redo |
| `editor_viewport_vim.cpp` | 692 | the Vim modal engine (ADR 0046) |
| `editor_viewport_find.cpp` | 156 | find/replace matching and navigation |
| `editor_viewport_commands.cpp` | 190 | save/open, `runCommand`, compile + output polling |
| `editor_viewport_lsp.cpp` | 392 | LSP protocol, diagnostics ingestion, completion, hover |

**`editor_viewport.h` was not touched at all** — same class, same
members, same public API, same `Q_OBJECT`/moc output. Nothing was made
public and no `friend` was added, because nothing needed to be: C++
already allows one class's member functions to be defined in as many
translation units as you like.

### Why this rather than extracting real subsystem classes

Extracting `VimEngine`/`LspSubsystem`/`Renderer` classes was considered
and deliberately deferred. Every one of these concerns reaches directly
into `m_cursors`, `m_selectionAnchors`, `m_cache`, `m_lineStarts`,
`m_buffer` and `m_undo`; pulling them out means either passing the
viewport back in (a circular dependency wearing a hat), widening the
public API, or `friend` declarations. ADR 0046 already ruled on exactly
this question for Vim mode — "logic that reaches `m_cursors`/`m_cache`/
`m_buffer`/`m_undo` directly stays inline" — and that reasoning did not
change just because the file got long.

So this is an **organisational** refactor, not an architectural one,
and it is honest about that: coupling between these concerns is
unchanged. What it buys is navigability (no file over ~880 lines),
faster incremental builds (touching the Vim engine no longer recompiles
the renderer), and — the real prize — a *measurable* seam. Extracting a
genuine subsystem later now starts from a file that already contains
exactly that subsystem and nothing else.

### The shared-symbol surface is the useful signal

`editor_viewport_internal.h` holds only what genuinely has callers in
more than one of the nine files:

- `kCaretWidth` — the renderer draws with it; `ensureCursorVisible()`
  reserves scroll margin for it.
- `kTypingAnimationTicks` — `insertText()` stamps entries with it; the
  renderer measures their progress against it.
- `isUtf8ContinuationByte()` — every "step one character, not one byte"
  walk (renderer, Vim motions, Backspace).
- `isWordChar()` — Ctrl+D's whole-word match, Vim word motions, the
  completion prefix scan.

Everything else — 18 of the 23 symbols in the old file-wide anonymous
namespace — turned out to belong to exactly one concern and moved into
that file's own anonymous namespace. That four-symbol shared surface is
the evidence that the split follows real seams rather than arbitrary
line counts; a constant that later needs promoting into this header is
a signal that two files have started reaching into the same concern.

## Consequences

This was a pure code move, and was verified as one rather than assumed
to be:

- **Nothing lost, nothing duplicated**: the extraction ran off explicit
  line ranges, then a script checked that every one of the original's
  lines 151–3608 is covered exactly once. The only uncovered lines were
  the 15 blank separators between blocks; zero non-blank lines were
  missed and zero were duplicated. All 23 previously-file-scope symbols
  were confirmed to still be defined exactly once.
- **Clean build, zero warnings, first attempt**; `ctest` 9/9.
- **Live smoke test across all nine units**: file load + syntax colors +
  gutter (core/render), `j`/`w`/`o`/`u`/`:` (vim), typing and undo
  (edit), Ctrl+F with match highlighting (find), `Ctrl+=`/`Ctrl+0` zoom
  (config), `:w` clearing the dirty marker and writing to disk
  (commands), and clangd diagnostics rendering as a gutter dot plus
  squiggles (lsp).

The cost: `EditorViewport`'s implementation is no longer greppable in
one file, so "where does this live" now needs the table above (or a
`grep` across `editor_viewport*.cpp`). The 724-line header is
unchanged and remains the one place the whole class is visible at once
— which is now the *only* place, making it more load-bearing as
documentation than it was before.
