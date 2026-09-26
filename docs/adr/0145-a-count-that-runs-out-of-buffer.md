# ADR 0145: A count that runs out of buffer

## Status

Accepted

## Context

ROADMAP carried this, from
[ADR 0137](0137-a-count-before-an-insert.md)'s scope note:

> Counts on `s`, `S`, `C` and `c`-with-a-motion are ignored. The other
> six insert commands read theirs; these four each have their own
> deletion semantics to settle first.

Sixteen cases derived from vim say that entry was wrong. `3s`, `3S`,
`3C`, `3cw`, `c2w`, `2c2w`, `3cc`, `2cj`, `2ce` and `.` after them all
already matched vim exactly, with no change. Breaking the count on
purpose fails five of them, so they are not passing vacuously.

The note had guessed that these commands ought to *repeat the inserted
text* the way `3i` does. They do not — vim applies the count to the
deletion and inserts once — and the editor had that right from the
start. The gap was in the ADR, not the editor.

What the same exercise did find is a different gap, in every command
that takes a count of *lines*.

## Decision

One rule, vim's `cursor_down()`, applied everywhere a count reaches down
the buffer: **`count - 1` lines below, clamped to the last line — but if
there was nowhere to go at all, the whole command fails.**

The two halves both matter, and the pair is not obvious:

- `4C` on a three-line buffer from line 1 **works**, changing through
  line 3, because it could move even though not as far as asked.
- `2C` on the last line **does nothing**, because it could not move at
  all. Not "changes one line" — nothing.

Derived, not reasoned: the first result is what made me stop looking for
a rule like "fails when the count exceeds the buffer", which fits the
second case and contradicts the first.

`vimLineBelow()` now holds it, and `dd`, `cc`, `S`, `C`, `D`, `$` and
`j`/`k`-under-an-operator all ask it. The `j`/`k` path already had the
rule spelled out inline and was the only thing getting it right — which
is why `2cj` on the last line passed while `2S` on the last line did
not.

### Two smaller things underneath

**`$` ignored its count entirely**, in both normal mode and under an
operator. `3$` meant `$`, and `2c$` changed one line where vim changes
two. Normal mode clamps; under an operator it obeys the rule above,
which is what makes `2C` on the last line do nothing, since `C` is
`c$`.

**A linewise change ate the file's final newline.** The clamp used
`m_lineStarts.size() - 1`, which is the entry *past* a trailing newline
rather than the last line with text in it. `9cc` on a two-line file gave
`X` where vim gives `X\n`. `vimLastLine()` already existed for exactly
this and was not being used here.

## Consequences

**307 conformance cases**, up from 255. Thirty-one of the new ones cover
counts on the four commands and their edges; thirteen failed before this
change.

Two cases of mine were badly formed and were removed or rewritten rather
than kept. `2cb` started at column 14 of a thirteen-character line,
which is not a position vim and this editor agree on — the same mistake
[ADR 0136](0136-a-new-line-starts-where-the-last-one-did.md) recorded,
made again. And `2$x` on the last line gives the same answer whether the
cursor moved or not, so it proves nothing; `9$x` from the top of a
three-line buffer asks it properly.

The general lesson is cheaper than it sounds: a ROADMAP entry written
from a guess survived two releases and cost nothing to disprove. The
sixteen cases that found nothing were worth as much as the thirteen that
found something — they turned "we think this is broken" into "this is
covered".
