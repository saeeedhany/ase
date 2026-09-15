# ADR 0078: Visual-mode `r`, and a selection that is off by one

## Status

Accepted

## Context

`r` replaces one character ([ADR 0076](0076-replace-a-character.md)) and
`R` keeps replacing until Escape ([ADR 0077](0077-replace-mode.md)).
Visual-mode `r` is the third form: replace every character of the
selection at once.

Implementing it surfaced something larger than itself.

## Decision

### Line breaks inside a selection are left alone

A selection spanning three lines stays three lines. Every other
character becomes the target. Verified against vim: `VjrX` over
`abc/defg/hi` gives `XXX/XXXX/hi` in both, and a three-line file is
still three lines afterwards.

### It replaces exactly what visual `d` would delete

Not what vim would replace. Those are different, and the difference is
not in `r`.

**This editor's visual selection is exclusive of the character under the
cursor; vim's is inclusive.** Measured with `d`, which predates all of
this:

| keys | ase | vim |
|---|---|---|
| `vd` | deletes 0 characters | deletes 1 |
| `vld` | deletes 1 | deletes 2 |
| `vlld` | deletes 2 | deletes 3 |

It follows from the cursor model: the cursor sits *at* a byte offset
here rather than *on* a character, the gap
[ADR 0046](0046-native-vim-mode-phase-1.md) named and left open. Every
visual operator inherits it — `d`, `y` and `c` are all one character
short of vim.

So visual `r` matches `d`, and is one character short of vim in charwise
Visual for the same reason. Making `r` alone inclusive would have been
worse than the bug: the highlight would show one span and `r` would act
on another, and `vd` then `u` then `vrX` would touch different text.

Linewise Visual is unaffected and matches vim exactly, because the
linewise normalisation already snaps the range to whole lines.

### `.` repeats it, by replaying the whole visual command

[ADR 0075](0075-repeating-the-last-change.md) said visual operators were
not recorded, because vim repeats them over a same-sized region and that
looked like a different mechanism. It is not: replaying the keys `vlrX`
from a new cursor re-selects the same span and replaces it, which *is* a
same-sized region. Recording the keys gives vim's behaviour for free.

One fix was needed. `vimRecordKey()` cleared the recording whenever a
key arrived with nothing pending, which in Visual mode is every key — so
`v`, `l`, `r` each wiped what came before. It now leaves the recording
alone while in Visual mode, because a visual command is one command from
the first key to the operator.

## Consequences

The off-by-one is now documented but not fixed. Fixing it means making
the selection inclusive of the cursor character across `d`, `y`, `c`,
`r` and the highlight rendering at once, and deciding what that means
for the non-vim selection model the same code serves
([ADR 0019](0019-selection-model.md)). That is its own change, and a
larger one than any key.

Visual `r` with a non-printable target does nothing and leaves Visual
mode, rather than replacing with a control character.
