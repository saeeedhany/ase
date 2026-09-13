# ADR 0069: Find-in-line, and one way to move through any list

## Status

Accepted

## Context

Two requests from use, unrelated on the surface and the same underneath:
both are about not leaving the home row.

## Decision

### `f` `F` `t` `T` `;` `,`

Vim's find-in-line motions: `f` jumps forward to a character on this
line, `F` backward, `t`/`T` stop one short of it ("till"), `;` repeats
the last one and `,` repeats it in the other direction.

They are confined to the cursor's own line, as in vim, and that
confinement is the point rather than a limitation: it is what makes them
safe to fire without looking, because the worst case is that nothing
happens.

Three details worth stating, all of them vim's behaviour and all of them
easy to get subtly wrong:

- **The key after `f` is data, not a command.** `f3` searches for `3`
  and `fd` searches for `d` — the pending-target check runs before
  counts and before operator letters, or those two would be swallowed as
  a count and a delete.
- **A count fails as a unit.** `3fx` is the third `x`; if there is no
  third, the cursor stays put rather than settling on the second. A
  partial jump is worse than none, because you cannot tell it happened.
- **`f` is an *inclusive* motion under an operator.** `df,` deletes
  through the comma, unlike `dw` which stops before the next word. That
  inclusiveness is most of why these motions are worth having.

`,` is stored as its command letter rather than a direction flag, so
reversing is a four-case swap (`f↔F`, `t↔T`) and the repeat logic has
one shape instead of two.

### `Ctrl+J` / `Ctrl+K` move any list

Every list in this app is driven from a text field you are still typing
into: the completion popup while you type an identifier, the file
browser while you type a filter, quick open while you type a query. The
arrow keys mean leaving the home row mid-word, and `j`/`k` are already
what Normal mode has trained your fingers on.

The arrows keep working everywhere they did. This adds a way; it does
not replace one.

**One shared helper** (`gui/src/list_navigation.h`), not the same
two-key check copied into four event filters — the same argument
ADR 0053 makes about animation durations. A rule re-implemented per call
site drifts, and a list that answered `Ctrl+J` in three panels but not
the fourth would be worse than one that never did.

Wired into the completion popup, the file browser (Open, Save-As and
quick open all share it), and the project-search results list. That last
one needed an event filter of its own: the list has focus there, so its
keys never reached the panel's own handler — a thing worth knowing
before adding the fifth list.

## Consequences

Verified live on `alpha, beta, gamma, delta`:

- `f,` from column 1 lands on column 6, the first comma.
- `;` advances to column 12, `,` goes back to column 6.
- `3f,` lands on column 19, the third comma.
- `t,` lands on column 5, one short of it.
- `df,` leaves ` beta, gamma, delta` — the comma removed with the word,
  which is the inclusive rule above.

And for lists: `Ctrl+J` twice then `Ctrl+K` walks the quick-open list
down two and back up one, and does the same to a four-entry clangd
completion popup.

`ctest` 9/9, clean build, zero warnings.

Still deferred from ADR 0046's list, and now conspicuous by their
absence next to `f`: text objects (`ciw`, `di(`), which are the other
half of what makes vim's operators feel like a language, and dot-repeat,
which would make `;` far less necessary than it currently is.
