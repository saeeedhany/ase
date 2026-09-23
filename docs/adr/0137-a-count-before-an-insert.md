# ADR 0137: A count before an insert

## Status

Accepted

## Context

[ADR 0136](0136-a-new-line-starts-where-the-last-one-did.md) noted in
passing that `3ob` opened one line where vim opens three, and recorded it
as a separate gap. Looking at it properly, the gap was wider than that:
**none** of `i`, `a`, `I`, `A`, `o` and `O` read their count. All six
ignored it.

## Decision

A count repeats the whole insert when it ends. Derived from vim rather
than assumed, which mattered — three of the rules are not what "repeat it
N times" would suggest on its own:

| keys | result |
| --- | --- |
| `3iab` | `ababab` at the point |
| `3aab` / `3Aab` / `3Iab` | the same, at that command's position |
| `3oab` | three new lines, each `ab` |
| `2ia<CR>b` | `a⏎ba⏎b` — the **bytes** repeat, not the keystrokes |

`o` and `O` open a line per repeat; the rest repeat inline. That is the
only difference between them, so one mechanism covers all six.

### The indent is taken back per line, not per session

[ADR 0136](0136-a-new-line-starts-where-the-last-one-did.md) removed the
auto-indent from the line you left without typing on. With a count there
are several such lines, and vim strips every one:

- `3o<Esc>` on an indented line leaves **three** empty lines, not three
  four-space ones.
- `o<CR><CR><Esc>` does the same, with no count involved at all — so the
  single-line version was already incomplete.
- `2ox<CR><Esc>` keeps the indent on the lines holding `x` and drops it
  on the blank ones.

So the tracked "line auto-indent wrote to" becomes a span, and leaving
Insert walks it bottom-up, stripping the lines that are still only
whitespace. Bottom-up because removing one line's indent shifts the
offsets of every line after it.

## Consequences

Fifteen conformance cases, every expectation vim's own. Twelve fail
without the repeat.

The last one to pass was `.` after `3o`, and it found a second hole:
`vimRepeatChange()` leaves Insert "the way Escape does" and had grown a
pass for the Replace count but not this one. Its replay also cannot use
the capture buffer — the text comes from `m_dotInserted` and capture is
suppressed during a replay — so the repeat takes the text as an argument
rather than reading it from where it usually lives.

What a count still does not reach: `s`, `S`, `C` and `c` with a motion
all enter Insert too, and none of them repeat. They were out of scope
here because each has its own deletion semantics to get right first, and
guessing at them is how the four failures above would have happened
quietly.
