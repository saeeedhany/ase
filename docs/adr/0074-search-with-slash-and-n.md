# ADR 0074: `/` search, and a needle that outlives its panel

## Status

Accepted

## Context

`Ctrl+F` opened a floating find bar and searched incrementally as you
typed ([ADR 0021](0021-find-replace.md)). Vim mode had no `/` at
all, which is the single most-used key it was missing.

The keys were never the hard part. [ADR 0067](0067-go-to-definition.md)
already settled that shape: `gd` and `F12` are one action behind two
bindings, one for vim muscle memory and one universal. `/` is to
`Ctrl+F` exactly what `gd` is to `F12`.

The hard part was lifetime. `FindBar::hideBar()` called
`clearFindQuery()`, which dropped the needle, the match list and the
current index together. The search died with the panel — coherent with
the glance-act-dismiss model the floating panels are built around, and
the reason closing the bar leaves the document visibly clean.

Vim's model is the opposite. You search once and then press `n` for the
rest of the session, with nothing on screen. `n` could not be built on
the existing `findNext()`, because with the bar closed there was nothing
left to repeat.

## Decision

### The highlight is panel state; the needle is session state

Closing the bar now clears the matches and stops drawing them, and keeps
the needle. `clearFindQuery()` still exists and still drops both, for
callers that genuinely want a reset.

This is the whole design. Everything else follows from it:

- The document goes clean when the bar closes, exactly as before. No
  lingering highlights for anyone who does not use `n`.
- `n` and `N` work with no UI on screen, which is the point.
- `n` repeats whatever was last searched for, however it was searched
  for — `Ctrl+F` and `/` feed the same needle, so there is one "last
  search" rather than two competing ones.

### The match list is not kept current

`recomputeMatches()` runs from `refreshCache()`, on **every edit**, and
it lowercases the whole buffer twice. Keeping the list live for a needle
nobody is looking at would have made every keystroke in a 276KB file pay
for a search closed ten minutes ago.

So the list is dropped with the highlights and rebuilt on demand by the
next `n`. An `m_findActive` flag gates the recompute, which keeps the
per-edit cost exactly what it was before this change.

### `/` and `?` are a prefix on the status-bar line

Not a new panel. [ADR 0073](0073-the-command-line-moves-to-the-status-bar.md)
moved the `:` line into the status bar precisely so this would be a
prefix rather than a second search surface. `Enter` dispatches on the
prefix character: `:` runs a command, `/` and `?` search.

`/` lands on the first match *past* the cursor rather than the nearest
one at or after it. Incremental search wants the latter — you are still
typing, and jumping away from what you can see is wrong. A search you
have finished typing should move.

### `n` keeps the search's direction

`n` after `?` goes backward, `N` forward. Direction is a property of the
search, not of the key. Both step from the cursor rather than from
`m_currentMatch`, which an edit or a rebuild can invalidate.

### Escape is `:nohlsearch`

Escape in Normal mode drops the highlights and keeps the needle — the
mapping most vim users add by hand, on the key they already press.

### `Ctrl+F` is untouched

It has two fields, which do not fit on one status-bar line, and vim's
own answer to replace is `:s/a/b/` through the command line anyway. The
split is by shape, not by audience: one field on the bar, two in the
panel.

## Consequences

`Ctrl+F` followed by Escape now leaves a needle behind where it
previously left nothing. That is deliberate and is what makes `n` work
for people who never type `/`, but it does mean a later `n` can jump
somewhere the user has forgotten they searched for. Escape clearing the
highlight, not the needle, is the compromise: the screen forgets, the
editor does not.

There is no `:noh` command and no `hlsearch` option to turn any of this
off. Neither has been asked for, and both are easy to add to a command
line that now exists.
