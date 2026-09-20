# ADR 0131: Replacing across files

## Status

Accepted

## Context

Project search has been read-only since
[ADR 0066](0066-project-wide-search.md): find every match, jump to one.
Editing them was deliberately left out, because "change every file at
once" needs an answer to two questions that finding does not.

LSP rename is waiting on the same two answers, which is why both have sat
on the roadmap as one item.

## Decision

### Undo is per file, and the preview is the safety mechanism

`ase_undo_undo(stack, buffer, ...)` — one stack, one buffer. The undo
stack has no notion of an operation spanning files, and giving it one
means a window-level history that owns every buffer's, with genuinely
ambiguous semantics: undo in a file you have since edited would silently
change others.

So a multi-file edit is *N* per-file edits applied together. Each file
gets exactly one undo group, so `u` takes back that file's whole share.
Reversing the entire operation means closing the buffers without saving.

That makes the **preview** the thing that keeps you safe, not undo. You
decide before, not after — every change is listed, `space` keeps or drops
one, `ctrl+enter` applies what is left.

### Applied into buffers, never onto disk

Every affected file is opened as a real buffer and edited through the
ordinary undo path. Nothing is written until you save.

This costs a tab per file, which is the honest price: the editor shows
you what it changed. In exchange it reuses the atomic save
([ADR 0109](0109-saving-without-losing-the-file.md)), crash snapshots
([ADR 0127](0127-the-buffer-with-nothing-to-fall-back-on.md)) and the
dirty marks, and "close without saving" is a complete reversal.

Writing straight to disk for files that are not open would be faster and
was rejected: no preview of the result, no undo at all, and it steps
around the save path that exists precisely to not lose files.

### Where the code lives

`applyLineEdits()` on the viewport applies one file's share as a single
undo group. It sorts and applies **last-first**, because every
replacement shifts the offsets of the ones after it, and it does not
trust the caller to have sorted. An edit naming a place the buffer does
not have is skipped rather than fatal — a hit can outlive the file it was
found in, and half a rename is worse than none.

`project::editsByFile()` groups accepted hits into per-file shares. It is
separate from `MainWindow` because `MainWindow` lives in `main.cpp`
beside `main()` and cannot be linked into a test; what is left there is
opening a buffer and calling the above, which has nothing to get wrong.

## Consequences

`Alt+R`, mirroring `Alt+F`. The find bar already had a replace row and a
project mode; this is both at once.

Three defects, each found by a different method, which is the part worth
recording:

**The first run of the unit tests** caught `offsetForLineColumn()`
counting from zero on both axes while every caller in this feature counts
from one. It clamps rather than refusing, so the error was silent — every
edit resolved to a place just inside the previous line and was rejected
by the range check.

**The first run in the actual editor** showed an empty panel.
`setMode()` read `mode == Mode::SearchResults` to decide whether the list
was visible, so adding a third mode hid the rows and showed an empty text
view — which looks exactly like a search that found nothing.

**Reading the preview it then drew** caught two more. `hit.text` is
trimmed but `hit.column` indexes the untrimmed line, so the preview drew
the replacement one indent late: `return widggadgetal;`. `SearchHit`
gained `textColumn` for indexing what is displayed.

And the last of them was the real one. `project::search()` returns **one
hit per line** — correct for a results list, where six near-identical
rows push real hits off the screen, and silently wrong for a replace,
where five of six surviving is the worst thing it could do. `search()`
now takes `everyOccurrence`, false for finding and true for replacing.

A search that returns a truncated result refuses to become a replace at
all. Replacing a prefix of the matches while implying it was all of them
is the one outcome worth refusing outright.

Rename is the next producer of the same edit set: a `WorkspaceEdit` is a
list of per-file ranges and replacements, which is what `editsByFile()`
already returns.
