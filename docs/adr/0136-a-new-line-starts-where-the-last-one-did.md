# ADR 0136: A new line starts where the last one did

## Status

Accepted

## Context

There was no auto-indent. `o` on `    int x = 1;` produced a new line at
column zero, and so did Enter in Insert mode. Every line typed inside
indented code had to be indented by hand.

This is the highest-frequency gap the editor had left — it is paid on
every newline in every file, which is more often than any of the larger
missing features would have been.

## Decision

A new line inherits the leading whitespace of the line it came from,
from `o`, `O` and Enter in Insert mode. `auto_indent` in the config,
**on by default**.

That default is a deliberate divergence. vim ships `autoindent` off, so
`vim -u NONE` has it off — and nearly every real vimrc turns it on, as
does every other editor. Off by default would be faithful to a vim
nobody runs.

### Copied verbatim, not re-rendered

The whitespace is copied exactly as found. vim normalises it according
to `expandtab` — a tab-indented line yields eight spaces with `expandtab`
set, and `  \t` yields one tab without it. Measured both ways to be sure.

This editor has no tab policy to normalise towards, and converting a
file's existing indentation because someone pressed Enter is worse than
inheriting whatever the file already uses. So a tab-indented file stays
tab-indented, and mixed indentation is preserved exactly.

## Consequences

Eleven cases added to the conformance suite, and because vim's own
default is `noautoindent`, they had to be derived from a vim configured
the way this editor behaves — `derive_cases.py` grew a second list with
`set autoindent`. Without that they would have asserted the opposite of
the feature.

**Four of the first twelve failed**, which is the entire argument for
deriving rather than guessing:

**The indent replaces the remainder's whitespace, it does not prepend to
it.** Splitting `    hello| world` gives `    world` in vim, not
`     world`. Confirmed by deriving the same case with `noautoindent`,
which gives ` world` — so the space survives without the feature and is
consumed with it. Nothing about "inherit the indentation" implies that,
and it would have shipped wrong.

**An indent you never typed on is taken back.** `o<Esc>` leaves the line
empty, not four spaces wide. Implemented, and then it broke a different
case: the cursor sat inside the whitespace being removed, so it clamped
to the end of the buffer and the next `o` opened after the wrong line.
Repositioning it to the start of the emptied line is what the fourth
failure was.

**Counts on `o` and `O` are not implemented at all.** `3ob` produces one
line here and three in vim, with autoindent *and* without — so it is a
separate gap this feature merely walked past. The case was removed rather
than kept passing by accident, and the gap is recorded in ROADMAP.

The remaining failure was a badly formed case of my own: `a` at a column
past the end of the line, which is not a position vim's `cursor()` and
this editor agree on. Replaced with `A`, which asks the same question
unambiguously.

231 conformance cases now, all of them still what real vim produced.
