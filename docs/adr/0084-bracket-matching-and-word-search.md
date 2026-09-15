# ADR 0084: `%`, `*`/`#`, and where a search lands

## Status

Accepted

## Context

Two more motions, and a correction to one already shipped.

## Decision

### `%` matches the bracket at or after the cursor

Vim does not require the cursor to be *on* a bracket: it scans forward
along the line for the first one and matches that, which is what makes
`%` usable from the start of a line of code. Nesting is counted, the
partner is searched for over the whole buffer, and `()`, `[]` and `{}`
are all handled.

As an operator target it is inclusive in both directions, so `d%` from
either bracket takes the pair and everything between. Brackets inside
strings and comments are not skipped, because plain vim does not skip
them either.

`{count}%` — vim's "jump to count percent of the file" — is not
implemented; a count is ignored.

### `*` and `#` search for a whole word

The word under the cursor, or the next one on the line when the cursor
is not on one. Matching is whole-word, so `*` on `foo` skips `foobar`
and `xfoo`, which needed a `m_findWholeWord` flag on the match scan that
`/` and the find bar leave off.

They set the search pattern, so `n` afterwards keeps going — the needle
already outlives its panel ([ADR 0074](0074-search-with-slash-and-n.md)),
so this came for free.

They also step off the current occurrence deliberately: forward searches
from past the word's end, backward from its start. Without that, `*` on
a word lands back on the word it started from.

### A search lands on the match, not past it

`jumpToMatch()` put the cursor at the *end* of the match and selected
it, which is what the find bar's replace acts on. Vim's search puts the
cursor on the first character and selects nothing.

So `/`, `?`, `n`, `N`, `*` and `#` now ask for the non-selecting form.
This is a correction to [ADR 0074](0074-search-with-slash-and-n.md),
which got the search working but left it landing a match-length too far
along — `/beta` stopped at column 11 where vim stops at column 7.

The find bar keeps the selecting form, and its replace is byte-identical
before and after, checked against the previous build.

## Consequences

Nineteen cases were checked against vim 9.2 by running the same keys
through `vim -u NONE -N` and comparing the cursor line and column, or
the buffer for the operator cases: `%` on each bracket kind, from a
closing bracket, nested inside and out, off a bracket, unmatched, across
lines, with `d`, `y` and `c`, and repeated with `.`; `*`, `*` twice, `*`
then `n`, `#`, `#` twice, `#` then `N`, `*` off a word; and `/`, `n`,
`N`, `?` for the landing position. All match.

Two comparisons initially looked like failures and were neither: vim's
`normal!` abandons the rest of a sequence when a motion fails, so a
failed `%` swallowed the `x` after it, and one test sent four cursor
keys where vim got five.
