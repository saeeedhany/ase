# ADR 0018: Undo/redo — diff-based groups on top of the buffer's own primitives

## Status

Accepted

## Context

Undo/redo has been a known gap since ADR 0005, which chose a piece
table specifically because it made undo "simpler... on top of this:
each edit already produces a small, cheap-to-record diff" — but the
diff-recording layer itself was never built. It's a real standalone
gap in a "complete normal editor," not only a Vim prerequisite (Vim's
`u`/`Ctrl-r` is deferred further out — see `docs/ROADMAP.md`).

The one design wrinkle specific to this editor: a single keystroke with
multi-cursor active (ADR 0012) already issues *N* buffer operations,
one per cursor, processed **highest-offset-first** so no cursor's
recorded offset is invalidated by another cursor's edit in the same
batch. Undo has to treat that whole batch as one user-visible action,
and has to unwind it in a way that respects the same ordering
constraint the forward pass relies on.

## Decision

New core module, `core/include/ase/undo.h` + `core/src/undo.c`,
`AseUndoStack` (opaque, alongside `AseBuffer`):

- Records **edit intent, not snapshots**: each entry is `{offset,
  inserted bytes XOR removed bytes}`. `EditorViewport`'s edit
  primitives (`insertTextAt`, `deleteBackwardAt`, `deleteForwardAt`)
  call `ase_undo_record_insert`/`_delete` right alongside the matching
  `ase_buffer_insert`/`_delete` call — for deletes, the removed bytes
  are read out of `m_cache` *before* the buffer call, since
  `ase_buffer_delete` doesn't hand back what it removed and `m_cache`
  is already known-correct at that position (unaffected by any
  higher-offset cursor processed earlier in the same batch, by the same
  invariant that already lets multi-cursor deletes skip cross-cursor
  bookkeeping).
- **Groups, not individual entries, are the undo/redo unit.**
  `ase_undo_begin_group`/`_end_group` bracket each of `insertText`/
  `deleteBackward`/`deleteForward`'s per-cursor loops in
  `EditorViewport`, each call snapshotting the caller's current cursor
  offsets (copied in, not referenced) so undo/redo can restore them
  exactly rather than re-deriving a position. A group with zero
  recorded entries (every cursor's op was a no-op, e.g. Backspace at
  offset 0) is discarded rather than pushed — nothing to undo, and a
  no-op keystroke shouldn't clear redo history.
- **Undo unwinds in the exact reverse of application order** (not
  regrouped by offset): entries were recorded highest-offset-first, and
  each entry's offset is only guaranteed valid in the buffer state that
  existed right before *that* entry was originally applied. Redo
  reapplies in the original forward order — the order already proven
  safe. Both use `AseBuffer`'s two existing mutators
  (`ase_buffer_insert`/`_delete`) as the only way they touch the
  buffer — no new buffer-internal access.
- A new edit clears redo history (standard semantics), but only once a
  group actually has entries — an all-no-op group leaves redo alone.
- GUI wiring: one `AseUndoStack *m_undo` on `EditorViewport`, created/
  destroyed alongside the buffer. `Ctrl+Z`/`Ctrl+Shift+Z` call new
  `undo()`/`redo()` methods, which apply the returned cursor snapshot,
  `refreshCache()`, and `snapAnimationToTarget()` — an undo/redo renders
  instantly, the same reasoning ADR 0017 already established for
  typing: gliding can't keep pace with rapid actions and just reads as
  lag.
- Deliberately **no coalescing of consecutive keystrokes into one
  group** — each `keyPressEvent` call is its own group, so typing three
  characters is three separate undo steps, not one. Simpler, and
  matches how several real editors behave without a "typing session"
  heuristic; revisit only if it's reported as annoying in practice.

## Consequences

Tested in `core/tests/test_undo.c` (single insert/delete round-trips,
grouped multi-cursor undo/redo, redo cleared by a new edit, a no-op
group not polluting the stack, undo past the beginning as a no-op not
a crash) — all passing under ASan/UBSan alongside the rest of the
suite. Verified live: typing "123" then one `Ctrl+Z` removes only the
"3" (per-keystroke granularity, as designed); two simultaneous cursors
typing "Z" at once, then one `Ctrl+Z`, revert both together. Every
later phase in the "complete normal editor" plan (selection-delete,
cut, paste, replace-all) routes through the same `begin_group`/
`record_*`/`end_group` calls this phase established, rather than
needing its own undo plumbing.
