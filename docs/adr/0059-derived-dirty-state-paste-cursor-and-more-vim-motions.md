# ADR 0059: Dirtiness is derived, paste lands where vim lands, and four more motions

## Status

Accepted

## Context

Two defects from an external tester, plus two motions requested
alongside them. The tester's third report — "LSP doesn't work from the
AppImage or the .deb" — is real but has a different cause and is handled
separately.

## Decision

### `m_dirty` was a latch; dirtiness is a comparison

The bug as reported: type a space, delete it, and the file is still
marked modified — the tab keeps its dot, the title keeps its `*`, and
quitting still asks about unsaved changes for a file that is byte-for-byte
what is on disk.

`m_dirty` was set `true` at twelve mutation sites and cleared in exactly
one, on save. **It recorded that the buffer had been touched, not that
it differed from disk** — those are different questions, and only the
second one matters. ADR 0023 shipped this knowingly, and
`applyUndoResult()` carried a comment calling it "a minor cosmetic paper
cut". It isn't cosmetic: it makes the unsaved-changes confirmation cry
wolf, and a prompt that fires when nothing is wrong is a prompt people
learn to dismiss without reading.

The fix is vim's own model, moved into `core/` where it can be tested.
`AseUndoStack` now carries a **state id**: a fresh stack is state 0 ("the
file as loaded"), every committed group is assigned an id that is never
reused, and undo/redo move *between* existing states rather than
creating new ones. So two observations of `ase_undo_state_id()` are
equal exactly when the content is the same. `EditorViewport` records the
id at save and `isDirty()` is that comparison — derived, never latched.

Deliberately **not** a group count: undo one group, type a different
one, and the count is back where it started while the content is not.
That is a unit test, not a footnote (`test_state_id_is_not_a_group_count`).

One case a state id cannot describe, so it is named rather than papered
over: a plugin command mutates the buffer directly and the undo history
is destroyed, which resets the id to "as loaded" for a buffer that
plainly isn't. `runPluginCommand()` sets an explicit
`m_historyDiscardedWhileDirty` flag, cleared by the next save.

A content hash would also be correct and was rejected: it is O(file) per
keystroke, against an editor whose pitch is "fast as hell" and whose
last performance fix was about exactly this kind of per-frame scan
(ADR 0053).

### Charwise paste leaves the cursor on the last pasted character

Real vim: charwise `p`/`P` leave the cursor on the **last** character of
what was pasted. This editor left it on the first, which is why a second
`p` re-pasted into the middle of the first paste instead of continuing
after it.

The tester asked for "the end of the pasted text" in both cases, and
that half of the request is declined: **linewise paste was already
right.** Vim puts the cursor on the *first non-blank of the first pasted
line* for `p` after a `yy`, and that is what `vimFirstNonBlank` branch
already did. Changing it would have traded a real bug for a new one.

### `{` and `}` — paragraph motions

Previous/next **empty** line. Whitespace-only lines are not boundaries:
vim's paragraph boundary is a genuinely zero-length line, and treating
an indented blank as one stops the motion in places that look like
text.

The scan starts from the line after (or before) the cursor's own, so
repeating the motion through a run of blank lines advances one at a
time, as real vim does. Falling off either end lands at the start/end of
the buffer rather than refusing to move.

They are ordinary charwise motions, so they compose with operators for
free: `d}` deletes to the next blank line, `y{` yanks back to the
previous one, and both extend the selection in Visual mode.

### `Ctrl+U` / `Ctrl+D` — half a screen, cursor and view together

Vim moves *both*: half a screen of lines, with the viewport following by
the same amount so the cursor keeps its row and the text slides under
it. Scrolling without moving the cursor (what the wheel does here) or
moving without scrolling (what every other motion does, via
`ensureCursorVisible`) would each read as a different gesture. The move
goes through `moveCursorVerticallyAt`, so the remembered column behaves
exactly as it does for `j`/`k` and the arrows.

**This is the one place Vim mode takes over an existing Ctrl shortcut
rather than adding one, and it reverses a rule ADR 0046 set
deliberately.** That ADR kept the entire Ctrl chain mode-independent, so
`Ctrl+S`, `Ctrl+Q` and friends never depend on which mode you are in,
and it is still right for all of those. But `Ctrl+D` is
half-a-screen-down to anyone with vim in their fingers, and having it
fan out multi-cursors in Normal mode is a surprise that costs more than
the consistency saves. The override is scoped as narrowly as it can be:
**Normal and Visual only.** Insert mode and `vim_mode = false` keep
multi-cursor `Ctrl+D` untouched — which is where multi-cursor editing
actually happens, since it is a typing-time tool.

The shortcut reference says so on both rows rather than leaving two
entries that contradict each other.

## Consequences

Verified live, on a real file:

- Type a space → the tab shows its unsaved dot and the status bar its
  `*`; `u` → both gone; `Ctrl+R` → both back; `u` → gone again.
- `yw` then `p` on `alpha beta gamma` yields `aalpha lpha beta gamma`
  with the cursor at **Col 7**, on the trailing space of the pasted run
  — the last pasted character, where vim leaves it.
- `}` from line 1 lands on line 2, again on line 5, and `{` returns to
  line 2 (the file's two blank lines).
- `d}` from the first line of a paragraph removes exactly that paragraph
  and leaves the blank line.
- `Ctrl+D` from line 2 lands on line 24 with the viewport scrolled to
  match; again to 46; `Ctrl+U` back to 24.
- With `vim_mode = false`, `Ctrl+D` still selects all three occurrences
  of a word.

`ctest` 9/9, clean build, zero warnings — the undo suite gained three
state-id cases (return-after-undo, not-a-group-count, no-op group).

Still deferred, unchanged from ADR 0046: registers beyond the clipboard,
macros, marks, text objects, dot-repeat, `Ctrl+F`/`Ctrl+B` full-page
(`Ctrl+F` is Find here, and unlike `Ctrl+D` that collision has no scope
narrow enough to resolve cleanly), and search motions.
