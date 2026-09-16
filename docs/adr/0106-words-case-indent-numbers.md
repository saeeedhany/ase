# ADR 0106: WORDs, case, indent and numbers

## Status

Accepted

## Context

Four everyday vim commands were missing: `W`/`B`/`E`, `~`, `>>`/`<<`, and
`Ctrl+A`/`Ctrl+X`. None is large, and all four are verifiable against real
vim the same way everything else in `gui/tests/` is.

The suite found two bugs in the first one before it found any in the
other three.

## Decision

### `W B E` fold punctuation into the word

`w` stops at a class boundary, so `foo.bar` is three words to it. `W`
splits on blanks only. Rather than a second family of scanners, the
existing ones take a `big` flag and `vimClassifyAt` reports `Punct` as
`Word` when it is set.

Two defects came out of wiring it:

- `vimWordBackward`'s inner scan called `vimClassifyAt(prev)` without the
  flag, so `B` walked back like `b`. The outer scan had it; one call did
  not, and the result was a motion that looked implemented and was not.
- `cw`'s change-to-word-end rule was keyed on `m == 'w'` alone, so `cW`
  ran as `dW` plus insert and swallowed the space after the WORD.

Both are the same shape: a parameter threaded through most of a path.

### `~` toggles ASCII case, `count` characters, never past the line end

A byte outside ASCII is left alone rather than guessed at. The cursor
lands one past the last byte changed, clamped back on to the line — `~`
on the final character stays there.

### `>>` and `<<` are four spaces

Not `shiftwidth`: the render path has no tab-stop expansion (ADR 0031), a
literal tab measures about nothing wide, and `Tab` in Insert mode already
inserts four spaces. A second, configurable indent unit would be a second
answer to the same question.

They are operators, so `>j`, `2>>` and Visual `>` all work. `>` is
linewise whatever motion delimited it, and an empty line stays empty.
The lines are edited bottom-up inside one undo session, because each edit
shifts the offsets of the lines below it.

### `Ctrl+A` and `Ctrl+X` take the first number at or after the cursor

A run of digits, with a `-` immediately before it read as a sign. A `.`
is not part of a number, so `1.9` increments to `2.9` — vim's behaviour,
not an approximation of it. They are the only two Normal-mode commands
that are chords, so they are let through the modifier rejection at the
top of the key handler by name rather than by dropping the rejection.

That shadows `Ctrl+A` (select all) and `Ctrl+X` (cut) in Normal mode.
ADR 0046 set out to keep the Ctrl chain mode-independent and ADR 0059
already broke it for `Ctrl+D`/`Ctrl+U` on the same grounds: to anyone
with vim in their fingers these are increment and decrement, and Normal
mode is where they would reach for them. Insert mode, Visual mode and
`vim_mode = false` keep the editor's meanings, so the shadowing is
confined to the one mode that wants it.

## Consequences

Thirty-eight cases were added to `gui/tests/vim_cases.inc`, derived from
vim as always. Where a command's result does not reveal where the cursor
landed, the case chains an `x` after it — `~x`, `>>x`, `<C-a>x` — so the
landing is compared too, since the expectations are file contents.

The derivation script now sets `shiftwidth=4 expandtab` so vim's indent
matches the editor's unit. No other case is affected by it.

`<<` on an unindented line, `>>` on an empty one and `>><<` round-trip
all expect unchanged text. They are weak on their own, and they are the
cases that catch an over-eager `<<` eating a non-space or `>>` indenting
a blank.

133 key cases and 19 command cases pass, and the ten suites are green.
