# ADR 0075: Repeating the last change (`.`)

## Status

Accepted

## Context

`.` is the key that makes the rest of vim's grammar pay off: `cw` a word,
then `.` on the next one, and the next. Without it every edit has to be
typed in full, and the operator/motion vocabulary
([ADR 0046](0046-vim-mode-phase-1.md)) is worth much less than it looks.

It repeats the last *change*, which is a narrower thing than the last
command. Motions, mode switches, yanks and undo are all excluded — `yy`
followed by `.` repeats whatever change came before the yank, not the
yank.

## Decision

### Record the keys, not a description of the edit

The change is stored as the Normal-mode keys that made it, plus any text
typed in the insert session that followed, and replayed through the same
dispatcher that handled them the first time.

The alternative — a struct naming the operator, motion, count and
inserted text — was rejected because it needs a case for every command
and a new case for every command added later. Keys compose for free:
`c` plus any motion plus any typed text needs no special handling,
because the dispatcher already knows how to do that.

This is close to what vim itself does, and it is why `.` there repeats
things nobody designed it to repeat.

### A change is where the buffer is modified

`vimMarkChange()` is called from the points that actually change text:
`d` and `c` when an operator resolves, `x`, `p`, `P`, and the
insert-entry commands `i a I A o O`. Not from `y`, not from `u`, not
from any motion. Putting the call at the mutation rather than at the
dispatch is what keeps the exclusions honest without a list of them.

### Replaying must not rewrite what it replays

`m_dotReplaying` suppresses both recording and marking for the duration.
Without it the first `.` would record itself as the last change and
every later `.` would repeat a repeat.

The pending state has to be reset **before** the replay, not after. The
count that selected the repeat is still pending when it starts, so a
replayed digit is appended to it rather than starting fresh: `3x`
followed by `2.` accumulated 22 and took the whole line. Found by
driving the editor, not by reading the code.

### A count on `.` replaces the original

`3x` then `2.` deletes two characters, not six and not three. The
recorded keys have their leading digits stripped and the new count put
in front. This is vim's rule, and the useful one — the count is part of
the command you are redoing, so being able to redo it differently is the
point.

### Insert text is captured, not re-typed from keys

Keystrokes inside an insert session are not replayed as keys; the bytes
they inserted are captured and re-inserted in one go. Replaying them
individually would run each through completion triggers, the typing
pop-in animation and the LSP didChange path, for no gain.

## Consequences

Visual-mode operators are not recorded. Vim repeats those over a region
of the same size at the new cursor, which is a different mechanism from
replaying keys, and it is not built here. A `.` after a visual delete
repeats whatever the last Normal-mode change was, which is at least
predictable.

Backspace inside an insert session shortens the capture rather than
being recorded, stepping back over a whole codepoint. The capture has to
hold what the session actually put in the buffer, not the keys that got
it there: without this, typing "helo", Backspace, "lo" repeated as
"helolo". That was a real bug, found by driving the editor after the
rest of this already worked.

`r` is not implemented yet, so it is not repeatable. When it lands it
needs one `vimMarkChange()` call and nothing else.
