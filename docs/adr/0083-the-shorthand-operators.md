# ADR 0083: The shorthand operators (`s`, `S`, `C`, `D`, `X`)

## Status

Accepted

## Context

Five single-key commands that each stand for an operator and a motion:
`s` is `cl`, `S` is `cc`, `C` is `c$`, `D` is `d$`, and `X` is `x`
backwards. They are common enough that their absence was felt, and cheap
enough to add once the operators they lean on were right.

## Decision

### Each routes through the operator it is shorthand for

`s` and `C` build a range and hand it to the charwise operator, `S` to
the linewise one, `D` the same with `d`. Nothing about registers, undo
grouping, dot-repeat or Insert entry is restated — they inherit all of
it, which is the point of adding them after
[ADR 0080](0080-one-undo-step-per-insert-session.md) rather than before.

A count reaches the end of the count-th line down for `C` and `D`, so
`2D` takes the rest of this line and all of the next.

### `s` and `C` on an empty line still start typing

An empty range would otherwise return early and leave the editor in
Normal mode. Vim enters Insert, so `vimChangeOrInsert()` does too.

### `cc` empties the lines instead of removing them

This was a deviation the code already admitted to — "real vim leaves a
blank line here; v1 simplification". `S` would have inherited it, so it
was fixed rather than copied: the change now takes the lines' content
but not the final newline, which collapses the span to one empty line
and leaves the cursor on it. `S`, `2S`, `cc` and `2cc` all match vim.

## Consequences

Checked against vim 9.2 by running the same keys through
`vim -u NONE -N` and diffing the buffer. `s`, `3s`, `s` at a line end,
`s` on an empty line, `S`, `2S`, `S` on the last line, `cc`, `2cc`, `C`,
`2C`, `D`, `2D`, `X`, `3X`, `X` at column 1, `D` then `p`, `D` then `u`,
and `s` then `.` — all match, with one exception below.

`2D` that reaches the last line of the file leaves a trailing newline
vim does not: vim writes zero bytes, this writes one. The range itself
is right; the difference is that an emptied buffer here still holds the
byte. `dd` and `D` agree with vim exactly, so this is narrow.

A separate pre-existing difference was found while testing and left
alone: `vGd` deletes two bytes more than vim. It is identical before and
after this change, and belongs with the visual-to-end-of-buffer
behaviour rather than here.
