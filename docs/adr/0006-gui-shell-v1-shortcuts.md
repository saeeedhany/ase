# ADR 0006: Phase 2 GUI shell — deliberate v1 shortcuts

## Status

Accepted (each item below is meant to be revisited, not permanent)

## Context

Phase 2's scope (docs/ROADMAP.md) is a minimal Qt window with a
custom-painted viewport wired to the core buffer's edit operations —
proving the core/GUI boundary works end to end, not the final rendering
or input pipeline. A few shortcuts were taken to keep this phase small;
per spec section 6, they're flagged explicitly here rather than left
silent.

## Decisions

1. **The GUI mirrors the whole buffer into a `QByteArray` on every edit**
   (`EditorViewport::refreshCache`, called from every insert/delete),
   and rebuilds a line-start index by scanning it. This is `O(total
   buffer size)` per keystroke — fine for the files this phase is
   exercised against, but in tension with the "several hundred MB, no
   perceptible input lag" non-functional requirement for genuinely large
   files. **Revisit before claiming that NFR is met**: the fix is
   windowed/incremental reads from `AseBuffer` (read only the visible
   range plus margin, and maintain the line index incrementally) rather
   than a full mirror — the `ase_buffer_get_text` API already supports
   partial reads, this phase just doesn't use that capability yet.

2. **The cursor moves by byte offset with UTF-8 continuation-byte
   awareness only, not full grapheme clusters.** Left/right/backspace/
   delete won't split a multi-byte UTF-8 codepoint, but a single visual
   grapheme made of multiple codepoints (combining marks, some emoji)
   can still take more than one keypress to move across or delete. Full
   grapheme-cluster segmentation is deferred — not needed until real
   multilingual/emoji text shows up in testing.

3. **No IME/composition support.** Input arrives only through
   `QKeyEvent::text()` in `keyPressEvent`. Simple composed input (e.g.
   dead-key accents) generally still works because Qt resolves those to
   a final character before delivering the key event, but complex IME
   composition (CJK input methods) is not implemented — no
   `inputMethodEvent` handling exists yet. Deferred until non-Latin
   input is actually a target use case.

4. **No dirty-tracking or close/quit confirmation.** `Ctrl+S` saves to
   the path the editor was opened with (a no-op if none was given —
   no "Save As" yet); closing the window or quitting never prompts,
   consistent with autosave/crash-journaling being explicitly deferred
   (see docs/ROADMAP.md, ADR 0005). Don't mistake the absence of a
   prompt for "no data loss risk" — it's simply not built yet.

## Consequences

None of this is visible to the user as missing chrome — it's invisible
correctness/perf debt. Anyone picking up Phase 3+ work that touches large
files, non-Latin text, or crash safety should read this ADR first rather
than discovering these gaps by surprise.
