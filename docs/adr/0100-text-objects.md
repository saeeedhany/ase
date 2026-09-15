# ADR 0100: Text objects

## Status

Accepted

## Context

`ciw`, `di(`, `ca"` — text objects are the most-used part of vim after
motions, and the editor had none. `i` and `a` meant only "insert" and
"append", so `diw` deleted a word's worth of nothing and `ci(` did not
exist.

They are also the feature where vim's behaviour is least guessable. Half
the rules below were not what I expected before running vim and reading
the bytes back.

## Decision

### The rules came from vim, not from memory

Every case was derived by running the keys through
`vim -u NONE -i NONE -N -es` with `nofixeol` and diffing the written
file. Both flags matter and both had already produced a wrong answer
once: without `-i NONE`, viminfo carries registers between runs; without
`nofixeol`, vim appends a trailing newline and two identical results
look different.

**`iw` is a run of one character class**, not a word — on whitespace it
selects the whitespace, on punctuation the punctuation. `iW` is a run of
non-blanks.

**`aw` takes the trailing blanks, or the leading ones when there are
none.** That second half is why `daw` on a line's last word removes the
space before it rather than leaving a ragged end.

**A bracket object uses the enclosing pair, and the cursor counts as
inside when it is on either bracket.** When there is no enclosing pair it
searches forward on the line — `di(` from column 1 of `aaa (bbb) ccc`
deletes `bbb`, and from after the `)` does nothing. Quotes behave the
same way, which is how `di"` works with the cursor before the string.

**`a"` takes surrounding blanks; `a(` does not.** No principle links
them; it is simply what vim does.

### An inner block spanning whole lines is linewise

The rule I would not have guessed. `di{` on

```
C {
 d
} D
```

leaves `C {` and `} D` — the inner line goes entirely. But with the
brace mid-line (`A { a` / ` b } B`) the same keys join the lines
charwise. The promotion happens only when the `{` is last on its line
and the `}` is preceded by nothing but blanks.

`ci{` on such a block keeps the closing newline so there is a blank line
to type on — the same split `C` has from `D`
([ADR 0099](0099-vim-conformance-fixes.md)).

### `i` and `a` are recognised above the operator guard

`vimApplyNormalKey` abandons a pending operator before its switch —
*"operator pending, but this is neither a repeat nor a motion"*. `i` hit
that and the `d` was gone before the object could resolve. This is the
third feature to land on that guard, after marks and `d'a`; it is the
first thing to check when an operator silently does nothing.

Visual mode needed its own case, because there the operator is empty and
that guard never runs. Without it `vi(d` deleted the single character
under the cursor and looked almost right.

## Consequences

40 of 40 comparisons against vim match: `iw aw iW aW`, `i( a( ib`,
`i{ a{ iB`, `i[ a[`, `i< a<`, `i" a"`, `i' a'`, in Normal and Visual,
under `d`, `c` and `y`, across single-line, nested and multi-line
blocks, plus `.` repeat and the cases where nothing matches and the
buffer is left alone.

Typing cost is unchanged — the new work is two branches on the `i`/`a`
keys and a range computation that runs once per object, none of it in
`refreshCache`.

Not implemented: `ip`/`ap` (paragraph), `it`/`at` (tag), and counts
(`d2i{`). Nothing uses them yet and each is a separate shape.

Quote objects search only within the line, so a string spanning lines is
not an object. vim has the same limit.
