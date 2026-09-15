# ADR 0101: `J` joins lines

## Status

Accepted

## Context

`J` was the other missing vim command named in
[ADR 0099](0099-vim-conformance-fixes.md), alongside the text objects
that became [ADR 0100](0100-text-objects.md). Pressing it did nothing.

Joining looks like the simplest command in vim and is not: the space it
inserts has four exceptions, and which of them applies is not guessable
from the outside.

## Decision

### The spacing rules, taken from vim

Derived by running the keys and diffing the written file, not from
memory:

| lines | vim's result |
|---|---|
| `aaa` + `bbb` | `aaa bbb` — one space |
| `ee ` + `   fff` | `ee fff` — indent stripped, no second space |
| `gg` + `)hh` | `gg)hh` — no space before `)` |
| `kk   ` + `ll` | `kk   ll` — existing blanks kept as they are |
| `ii` + `` (empty) | `ii` — nothing appended, no trailing space |
| `` (empty) + `jj` | `jj` |

So a single space, unless the first line already ends in blank, either
line is empty, or the next begins with `)`. `gJ` skips all of it and
takes both lines verbatim, indent included.

A count joins that many **lines**, not that many times: `3J` makes one
line out of three, and `J` is `2J`. A count that overruns the end stops
there rather than failing.

### The cursor lands where the lines met

Not on the joined-on text and not at the line start: at the offset the
newline used to occupy — the inserted space when there is one, and the
next line's first byte when there is not. Checked against vim for `J`,
`3J`, the trailing-blank case and the `)` case; all four columns agree.

### Visual `J` joins what the selection touches

Every line the selection spans, however it was made — `VjJ`, `VjjJ` and
`vjJ` all behave as vim does, which is to say charwise or linewise makes
no difference here.

### One undo step for the whole join

`3J` is two joins internally and one `u` in vim. The loop runs inside an
undo session so it undoes as a unit, and `.` repeats the whole thing.

## Consequences

23 comparisons against vim match: the six spacing cases, counts of 1, 3,
4 and 10, `gJ` with and without indent, the last line, the line above
it, all three Visual forms, undo of `J` and of `3J`, `J` twice, and `.`
repeat.

**`J` on the last line ate the trailing newline** in the first version,
because `m_lineStarts` has an entry for the position past it and the
loop treated that as a line to join with. `vimLastLine()` — added for
`G` in [ADR 0099](0099-vim-conformance-fixes.md) — is the guard. That
phantom entry has now produced three separate bugs; anything asking
"is there a line below this one" should be asking `vimLastLine()`.

`gJ` is included because parameterising the space rule was smaller than
leaving it out, and it is the pair `J` is normally learned with. Neither
respects `'joinspaces'` — vim's two-spaces-after-a-period option — which
is off by default and has no setting here to read.
