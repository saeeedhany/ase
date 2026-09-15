# ADR 0097: Marks and macros

## Status

Accepted

## Context

Marks (`m`, `` ` ``, `'`) and macros (`q`, `@`) were the two largest
remaining gaps in the modal editing. Both were deliberately deferred
mid-stream once before; both are things a vim user reaches for without
thinking, and notices the absence of immediately.

They are grouped here because they need the same two pieces of existing
machinery — the pending-key mechanism that `r` and `g` use, and the
key-replay path the dot command already had — and because the questions
they raise are the same: what exactly is stored, and what happens when
the buffer moves underneath it.

## Decision

### A mark is a (line, column), like a jumplist entry

`m{a-zA-Z}` stores the cursor's 1-based line and column, buffer-local.
That is the same shape `MainWindow::JumpEntry` already uses
([ADR 0070](0070-the-jumplist.md)), and the choice is deliberate rather
than convenient: having two different notions of "a remembered position"
in one editor would be worse than the drift either of them has.

The drift is real and shared: an edit that adds or removes lines *above*
a mark leaves it pointing at the old line number, where vim adjusts it.
Storing a byte offset would move it the wrong way for a different set of
edits. Fixing it properly means adjusting every stored position on every
edit, which is a change to the buffer's contract and not this one.

`` ` `` jumps to the line and column; `'` jumps to the line's first
non-blank. Both check against real vim rather than memory — same file,
same keys:

| keys | vim | ase |
|---|---|---|
| `:3`, `13l`, `ma`, `G`, `` `a `` | 3,22 | 3,22 |
| `:3`, `13l`, `ma`, `G`, `'a` | 3,9 | 3,9 |

A mark jump records a jumplist entry, so `Ctrl+O` comes back from it —
verified, not assumed. Jumping to a mark that was never set says so
rather than doing nothing.

### A macro stores key events, not characters

`q{a-z}` records, `q` stops, `@{a-z}` plays, `@@` replays the last
played, and a count repeats (`3@a`).

The dot command records a `QString` of characters, which is enough for
what it repeats. A macro is not: `Escape` and `Backspace` carry no text,
so a macro of plain characters would silently lose exactly the keys that
end an insert or fix a typo. Recording keeps the key code, the modifiers
and the text, and replay reconstructs a `QKeyEvent` from all three.

Keys are recorded **before dispatch**, at the top of `keyPressEvent`, so
a macro replays what was typed rather than what it was interpreted as.
The `q` that ends recording is dropped afterwards — leaving it in would
mean every replay started a recording.

Verified against vim by setting the same register contents and comparing
the resulting buffer byte for byte, for a count (`3@a`) and for `@@`.

### Running away is the failure that matters

A macro may call itself — `qa` `j@a` `q` is a legitimate thing to type,
and vim runs it until it hits an error. Two limits stop it here: a
nesting depth of 32, and a budget of 200,000 replayed keys shared by an
entire outermost invocation.

Both say so when they stop. Unwinding silently would look like the macro
simply did less than it was asked to, which is the failure mode this
codebase keeps finding and removing.

The recorded keys are copied before replay, because a macro can re-record
the register it is playing from, and iterating a container while it is
rewritten is how that ends.

## Consequences

Uppercase marks (`A-Z`) are global — they name a file as well as a
position — and are covered by [ADR 0098](0098-global-marks.md), which
solves it the way this one predicted: by reusing the jumplist's entry.

### Marks as operator targets

Added immediately after the above, because a mark you cannot delete to
is half a mark. `'` is linewise and inclusive of both ends; `` ` `` is
charwise and exclusive, so the byte under the later position survives.
Both reuse the operator helpers the other motions already use.

Checked against vim on the same file, mark at (2,5), cursor at (4,6):

| keys | result |
|---|---|
| `d'a` | `one AAA` / `five EEE` |
| ``d`a`` | `one AAA` / `two DDD` / `five EEE` |
| `c'a` then `XX` | `one AAA` / `XX` / `five EEE` |
| `y'a`, `G`, `p` | the three lines appended at the end |

All four byte-identical to vim.

The wiring was not where it looked. `vimApplyNormalKey` abandons a
pending operator before its switch — *"operator pending, but this is
neither a repeat nor a motion"* — so `'` reached that guard and the `d`
was discarded before the mark case could run. A mark motion has to be
recognised **above** that guard, which is the sort of thing a
measurement finds in a minute and reading finds in an hour: the keys
arrived correctly, the switch was correct, and neither was the problem.

An operator over a mark that was never set changes nothing and says so.

Macros record what reached the viewport, so keys handled by the window
before it — `Ctrl+Tab`, `Ctrl+W`, `Ctrl+N`, the jumplist bindings — are
not captured. A macro that switches buffers mid-replay is not something
this supports.

Dot-repeat was re-checked against vim after this landed, since macro
recording hooks the same key path it does: `2x`, `j0`, `.` produces the
same buffer in both.
