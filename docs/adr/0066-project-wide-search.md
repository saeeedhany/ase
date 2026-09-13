# ADR 0066: Project-wide search

## Status

Accepted

## Context

Find (ADR 0021) searches the buffer you are looking at. Everything else
in the project was invisible — you could not answer "where else is this
called?" without leaving the editor. With quick open (ADR 0065) the two
pieces this needs already existed: a definition of "the project", and a
walk that produces its files.

## Decision

### The query rides in the find bar; the results ride in the output panel

Two different surfaces, because they are two different lifetimes.

Typing a query is glance-act-dismiss, exactly what `FindBar` already is
— so `Ctrl+Shift+F` opens it in a third mode, `Project`, with the badge
showing `G`. Same field, same theming, same Escape. Enter runs the
search and closes the panel.

**Results are not glance-act-dismiss.** You read a hit, look at the
code, come back, read the next one. A floating panel is exactly wrong
for that: ADR 0044 made panels block the document while open, which is
right for a file picker and hostile to a results list. The `OutputPanel`
is already the docked, non-modal place for "output you read while
looking at your code" (ADR 0025), so it grows a second mode: the same
panel, showing a list instead of streamed text.

Rows are `path:line:  text`, the shape every compiler, linter and grep
on the machine already prints, so it needs no explaining. A row
activates on a *single* click as well as Enter — requiring a
double-click to follow a hit is the friction that makes people stop
using a feature.

### The same file list as quick open

`project::collect()` again, so a search can never find a hit in a file
that `Ctrl+P` refuses to show. Two different answers to "what is in this
project" would be a bug in one of them, discovered confusingly.

Skipped inside that list: files over 1 MiB, and anything with a NUL in
its first kilobyte — the same binary heuristic grep uses, for the same
reason (cheap, and text files do not contain NUL).

### Case-insensitive literal matching

What the in-file find already does. Two searches in one editor that
disagree about whether `Foo` matches `foo` would be a worse surprise
than either choice is a limitation. No regular expressions yet.

**One hit per line.** A line containing the needle six times is one
place to look; six identical rows would push real hits off the screen.

### Synchronous, with caps that are admitted

The search runs on the UI thread, like the quick-open walk. Measured
with `grep` over the same file set as a proxy: this repository is ~30ms
for a rare identifier and ~8ms for a common word (it stops early). The
caps — 20,000 files, 1,000 hits — bound the worst case, and when one is
hit the header says `first N matches` rather than presenting a prefix
of the answer as the answer.

The lowercasing is done once per file rather than once per match
attempt, and the line counter walks forward to each hit instead of
re-counting from the top of the file. Both are the difference between
searching a tree and waiting for one.

### Opening a hit is two objects' work, joined in one place

A hit is a file *and* a line. Opening the file belongs to `MainWindow`
(it owns the buffer list); landing on the line belongs to
`EditorViewport`. Rather than either reaching into the other, the panel
emits `hitActivated(path, line)` and the window does both — and
`EditorViewport::goToLine()` is now public, which also let `:42` stop
duplicating the same four calls.

## Consequences

Verified live in this repository: `Ctrl+Shift+F` for `vimGotoLine`
reports `6 matches for "vimGotoLine" in 235 files` and lists them with
paths and line numbers; arrowing to the fourth and pressing Enter opens
`gui/src/editor_viewport_vim.cpp` as a new buffer with the status bar
reading `Ln 233, Col 1` — the line the hit named. A search with no
matches says so both in the header and as a message, since the panel may
be appearing for the first time and an empty list is ambiguous.

Focus lands on the results list, so Up/Down/Enter work immediately;
Escape hands focus back to the editor **without** hiding the results,
because looking at the code and coming back to the list is the whole
workflow.

Deferred, in the order I would do them:

- **Replace across files.** The obvious next step, and deliberately not
  bundled: editing every file in a project from one keystroke deserves
  its own design pass (a preview, an undo story that spans buffers) and
  should not ride in on a read-only feature.
- **A background thread**, when a repository big enough to make this
  stutter actually shows up. The caps make that a quality issue, not a
  hang.
- **Regular expressions**, which want to be shared with the in-file find
  rather than bolted onto one of them.
- **Highlighting the matched span** in each row; the hit already carries
  its column.
