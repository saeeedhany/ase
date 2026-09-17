# ADR 0122: The cursor on the line it selected

## Status

Accepted

## Context

Two reports.

`V` selected a line and put the caret on the line **below** it, with the
status bar agreeing — it read `Ln 4` for a selection of line 3.

And the command line could not be opened from the panel, so closing a
panel with `:q` meant first going back to the buffer to be allowed to
type it.

## Decision

### The linewise cursor sits on the last selected line

`vimNormalizeLinewiseSelection()` parked the cursor at the start of the
line *after* the last selected one, because that is where the
selection's range ends. The range was right; the caret was a line below
what was highlighted.

Both ends now sit at the start of their own line, and `vimVisualEnd()`
works the span out from the line numbers it already keeps. The cursor no
longer has to carry the range.

Two things fell out of that:

The **paint guard** asked `hasSelectionAt()`, which is
`anchor != cursor`. A linewise selection of one line now has both at the
same offset and still covers the line, so a single-line `V` highlighted
nothing. It asks what the operators ask instead — is there a range —
which is the question that was always meant.

The column is not preserved: the cursor lands at column 1 of its line
rather than keeping the column it had, where vim keeps it. A linewise
selection has no column, and it is the line being wrong that was
reported.

### The command line belongs to the window

It was a viewport command, so it could only be opened by the buffer, and
the buffer only hears keys when it has focus. `:q` to close a panel you
are reading is the obvious thing to type and it was unreachable from
there.

It is a window command now, like the buffer list and the jumplist. The
command it opens still runs against the buffer in front, which is what
makes `:q` close the panel (ADR 0118) rather than the buffer.

## Consequences

Moving it exposed a latent bug in `sequenceFor()`, which turns a chord
into a `QKeySequence` for a window shortcut. It spelled punctuation as
words — `Alt+Semicolon` — and Qt only parses the symbol, `Alt+;`. An
unparsed sequence is *empty*, and an empty sequence installs a shortcut
that can never fire, with no error anywhere. `Alt+;` simply did nothing.

It had been harmless until now because every other window shortcut used
letters or named keys. It uses the same spelling the help panel shows,
which is the one Qt accepts.

All 220 vim conformance cases pass unchanged, which is what says the
operator ranges survived the change to where the cursor sits: they test
what `V`, `Vj`, `Vd` and `Vy` do to the text, and none of that moved.
