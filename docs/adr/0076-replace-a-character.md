# ADR 0076: Replacing a character (`r`)

## Status

Accepted

## Context

`r{char}` was the last common single-key change missing from Normal
mode. It is also the cheapest one to add correctly, because the
machinery it needs already exists: `f`/`F`/`t`/`T` already wait for a
target character ([ADR 0069](0069-find-in-line-and-list-navigation.md)),
and `.` already repeats a change ([ADR 0075](0075-repeating-the-last-change.md)).

## Decision

### Verified against vim rather than from memory

vim is installed on the development machine, so every rule below was
checked by running the same keys through `vim -u NONE -N` and diffing
the resulting buffer, rather than recalled:

| keys | buffer | result |
|---|---|---|
| `rX` | `abcdef` | `Xbcdef` |
| `3rX` | `abcdef` | `XXXdef`, cursor on the **last** replaced character |
| `9rX` | `abcdef` | unchanged |
| `4l2rX` | `abcdef` | `abcdXX` |
| `4l3rX` | `abcdef` | unchanged |
| `3r<CR>` | `abcdef` | one empty line, then `def` |
| `rX` | empty line | unchanged |

All eight cases, including a `.` repeat, now match vim byte for byte.

### A count that does not fit does nothing

`3rX` with two characters left on the line replaces nothing at all — not
the two that are there. That is vim's rule and it is the right one: a
partial replace is a silent wrong answer, and a no-op is at least
visible as nothing happening.

### `r` writes no register

It is not a delete. Clobbering the unnamed register with one character
would make a following `p` paste that character instead of whatever was
last yanked, which is not what anyone means by `r`.

### Enter is detected by key code, not by text

`r<CR>` replaces the count characters with a *single* line break. The
newline case cannot be recognised from `event->text()`: Return arrives
as `"\r"` under X11 but as `U+0000` under the offscreen platform plugin,
which produced three NUL bytes in the buffer the first time this was
tried. The key code is the same everywhere, so that is what it branches
on. Any other non-printable target cancels, as it does in vim.

This is worth remembering beyond `r` — any future key that needs to tell
Return, Tab or Escape apart from typed text has the same problem.

## Consequences

`r` is repeatable for free: `vimMarkChange()` at the mutation is all it
needed, exactly as [ADR 0075](0075-repeating-the-last-change.md)
predicted. `rZ` then `.` on the next line works without a line of code
specific to it.

`R` (replace mode, overtyping until Escape) is not implemented. It is a
mode rather than a single change, so it needs its own insert-session
handling rather than another call to this.

Visual-mode `r`, which replaces every character in the selection, is
also not implemented — the same gap Visual-mode operators have in
[ADR 0075](0075-repeating-the-last-change.md).

`r` followed by a key that carries no text — an arrow, say — leaves the
buffer untouched and does not eat the key after it, which is what vim
does. Verified both ways: `r<Right>X` and `r<Right>j` each leave the
buffer exactly as vim leaves it. The mechanism by which the pending
state is dropped on that path was not traced, so treat it as observed
behaviour rather than as a guarantee the code states anywhere.
