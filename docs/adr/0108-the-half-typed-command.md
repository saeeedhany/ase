# ADR 0108: The half-typed command

## Status

Accepted

## Context

`EditorViewport` carries 153 data members. Eleven of them are one idea:
what has been typed so far in a command that has not resolved yet. `dd`
parks the operator between its two keys, `2d3w` parks both counts, and
`"a`, `m`, `q`, `f` and `r` each park the letter they are waiting on.

They were eleven separate members cleared by hand in
`resetVimPendingState()`, which is called from 36 places. Adding a
prefix meant remembering to add a line there, and the class of bug that
follows is well represented in this project's history: the
operator-abandoning guard in `vimApplyNormalKey` has caught four
separate features that forgot to account for it.

A second copy of the same knowledge had already drifted. The check that
decides whether a key begins a new command — and so whether a
half-recorded dot should be dropped — tested six of the eleven fields
and silently ignored `mark`, `macro`, `textObject`, `registerName` and
`awaitingRegister`.

## Decision

The eleven become a `VimPending` struct in `gui/src/vim_pending.h`.

`reset()` is `*this = VimPending()` rather than eleven assignments, so a
member added later is covered the moment it is declared. `count()`
replaces seven copies of `std::max(1, count1) * std::max(1, count2)`,
which is vim's rule that `2d3w` deletes six words. `idle()` is the
complete form of the drifted check.

All 93 references live in `editor_viewport_vim.cpp`, so nothing outside
the vim layer had to change.

## Consequences

Switching the drifted check to `idle()` changes behaviour in the five
cases it had been ignoring. Eight cases were added first to pin what vim
does there — `.` after `ma`, after `` `a ``, after `"ayy`, after `"add`,
after `ciw`, after a recorded macro, after `f`, and with a count — and
they pass both before and after the switch. That is evidence the two
forms agree on the paths the suite reaches, not proof they agree
everywhere.

This does not address the other 142 members. The vim state alone is
about 32 of them once the macro and dot-repeat fields are counted, and
the rest cluster by prefix into LSP, hover, find and render groups that
could be given the same treatment. `VimPending` was taken first because
it is the group with a live bug class attached, not merely the largest.
