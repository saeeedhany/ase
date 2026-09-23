# ADR 0139: The column a caret is aiming for

## Status

Accepted

## Context

Moving down through a short line and out the other side should come back
to the column you started in. Every editor does this, and vim calls the
remembered column `curswant`.

ROADMAP had it as "vertical multi-cursor movement doesn't track a sticky
column per cursor (only single-cursor mode does) — a minor,
rare-in-practice paper cut". That description was wrong twice over, which
is the interesting part of this change.

**It was not only multi-cursor.** `keyPressEvent()` cleared the
remembered column near the top, before dispatching the key. The arrow
keys are handled above that line, so Up and Down kept theirs. Vim's `j`
and `k` are handled *below* it, through `handleVimNormalOrVisualKey()`,
so they were handed a cleared column on every keystroke. The editor's
primary way of moving down a file never had a sticky column at all.

**It was not rare.** Any file with lines of different lengths, which is
all of them.

## Decision

The remembered column is per caret, and **self-validating**: stored
alongside the offset it was true at. A caret found somewhere else has
been moved by something that is not `j`/`k`, so its column is recomputed
from where it actually is.

The alternative was to clear it explicitly in everything that moves a
caret sideways. That is the model vim uses, and here it would have meant
finding every motion, every operator and every edit that assigns into
`m_cursors` — dozens of sites, where missing one leaves a caret
mysteriously jumping columns. Validating on read cannot be forgotten,
and fails safe: an unrecognised position recomputes, which is the old
behaviour.

The blanket clear in `keyPressEvent()` is gone. The remaining clears are
the ones that mean it: a click, a find jump, select-all.

## Consequences

Nine conformance cases derived from real vim, and the first set of them
was **useless** — the buffer was `aaaaaaaa\nbb\ncccccccc\n`, and deleting
one character from a run of identical characters looks the same whichever
column you deleted it in. Seven of the nine passed against code that had
no sticky column whatsoever. Rewritten with every line distinct
(`abcdefgh\nij\nklmnopqr\n`), eight of nine failed immediately.

A case that passes is only evidence if it could have failed.

### A caret was allowed to stand on the newline

Two cases still failed after the column was fixed, and they were a
separate bug the new cases had walked into: `offsetForLineColumn()`
clamps a column to the line's length, which is the position *after* the
last character. Correct in Insert mode; in Normal mode vim's caret is on
a character. So `j` onto a short line left the caret on the line break,
and `x` there deleted the newline and joined two lines together.

`vimClampOffLineEnd()` now steps it back one character — UTF-8 aware, and
only outside Insert mode, and not on an empty line, where there is
nowhere to step back to.

### Two carets in one place are still one caret

The first multi-caret test asserted that carets crossing a very short
line stay separate. They do not: they land on the same offset and
`normalizeCursors()` merges them, as every multi-caret editor does. The
sticky column cannot preserve a distinction the positions no longer
carry. The test now uses a middle line that is too short for one caret
but not for both, which is the case the feature is actually for.

255 conformance cases, all of them vim's own answers. Six of them and
both multi-caret tests fail if the validity check is forced false.
