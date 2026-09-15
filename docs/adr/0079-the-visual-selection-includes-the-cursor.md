# ADR 0079: The visual selection includes the character under the cursor

## Status

Accepted

## Context

[ADR 0078](0078-visual-mode-replace.md) recorded that this editor's
Visual selection was exclusive of the character under the cursor where
vim's is inclusive, and that every visual operator inherited it:

| keys | ase before | vim |
|---|---|---|
| `vd` | deleted nothing | deletes one character |
| `vld` | deleted one | deletes two |
| `vlld` | deleted two | deletes three |

`vd` doing nothing at all is the sharp end of it.

## Decision

### Two bugs that were cancelling each other

The selection was not the only thing off by one. `$` as a pure motion
landed *past* the last character, where vim leaves the cursor *on* it:
`$x` deleted nothing here and `abcde` in vim.

Those two errors cancelled for `v$`: an exclusive selection ending at
the line end covers the same bytes as an inclusive one ending on the
last character. Fixing the selection alone would have turned `v$d` from
correct into a line-joining delete that swallowed the newline. They had
to be fixed together, which is why one ADR covers both.

### The adjustment lives in one accessor, not in the selection

`vimVisualEnd()` returns one character past `selectionMaxAt()` while
charwise Visual is active, and the plain value otherwise.
`selectionMinAt()`/`selectionMaxAt()` are unchanged, because the same
machinery serves the editor's own selection — shift+arrows, mouse drag,
`Ctrl+D`, find matches — which is exclusive and should stay that way
([ADR 0019](0019-selection-model.md)). Vim's rule is vim's, and it
applies where vim's mode is active.

It never extends over a line break, so a charwise selection sitting on
the last character of a line covers that character and stops.

Linewise Visual is untouched: the linewise normalisation already snaps
the range to whole lines, and extending it would have swallowed the
following line's first character.

### `hasSelectionAt()` stopped being the right question

With an inclusive range, anchor == cursor still means one character is
selected, so the `if (hasSelectionAt(0))` guard on visual `d`, `y` and
`c` was exactly what made `vd` do nothing. They now ask whether the
range is non-empty.

### `$` stays exclusive as an operator target

`d$` still deletes to the line end. The operator branch of
`vimExecuteMotion()` computes `$` for itself, so only the pure-motion
path moved — the same inclusive-motion distinction `f` and `t` already
make.

## Consequences

Fifteen cases were checked against vim 9.2 by running the same keys
through `vim -u NONE -N` and diffing the buffer, covering `v`/`V`
with `d`, `y`, `c` and `r`, charwise and linewise, single-line and
multi-line, `$` as a motion and as an operator target. All match.

Anything that yanked a visual selection now yanks one more character,
which is the point, and is visible as a changed paste in the screenshot
battery.

`cw` remains unlike vim's: vim treats `cw` as `ce` and leaves the
trailing space, this editor uses the plain `w` motion and consumes it.
Confirmed unchanged by this work — it predates it and is its own fix.
