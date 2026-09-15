# ADR 0081: `cw` changes to the end of the word

## Status

Accepted

## Context

`cw` was `dw` with a different operator: it changed through to the start
of the next word, eating the space between them. Vim leaves that space —
`cw` on `ab cd` gives `Z cd`, not `Zcd`. It is the most-used operator
pair in the editor, so the difference showed up constantly.

## Decision

### The special case is vim's, and it is narrow

`:help cw` states it: when the cursor is in a word, `cw` changes only up
to the end of that word. On whitespace it stays a plain `w`. Both halves
were checked rather than assumed, by forcing the cursor to column 1 with
`0` first — vim's `-es` mode otherwise starts at the first non-blank and
hides the whitespace case:

| buffer | `cw` | `ce` |
|---|---|---|
| `ab cd` | `Z cd` | `Z cd` |
| ` ab cd` | `Zab cd` | `Z cd` |

### It is not `ce`

The obvious implementation — treat `cw` as `ce` — is wrong, and the
matrix says where. `e` steps on to the *next* word when it is already at
the end of one, so on `.ab` it reaches the end of `ab`; both this editor
and vim agree that `ce` there changes the whole line. But vim's `cw`
changes only the `.`.

So "the end of the word" means the end of the character-class run the
cursor is sitting in, which is what `vimWordRunEnd()` now returns. Only
the first step of a count is special — `c2w` on `ab cd ef` gives `Z ef`,
so the second step behaves like `e`.

### Nothing else moves

`dw`, `d2w` and `yw` are untouched: the special case is gated on the
pending operator being `c`. `ce` is untouched, including on punctuation,
where it and `cw` now correctly disagree.

## Consequences

Sixteen cases were checked against vim 9.2 by running the same keys
through `vim -u NONE -N` and diffing the buffer — word, punctuation,
one and two leading spaces, mid-word, last word on a line, at the line
end, and counts up to three — plus the `dw`/`yw`/`ce` cases that had to
stay put. All match.

`vimClassifyAt()` indexes the cache without a bounds check, so the new
caller guards the offset itself. Worth knowing before the next caller.
