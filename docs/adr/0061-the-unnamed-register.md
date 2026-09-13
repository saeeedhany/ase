# ADR 0061: The unnamed register

## Status

Accepted

## Context

Vim mode has shipped since ADR 0046 with the clipboard standing in for a
register: `y` wrote the system clipboard, `p` read it, and `d`/`x`/`c`
wrote nothing at all. So **`dd` then `p` — vim's most common idiom, the
way you move a line — did nothing**, or worse, pasted whatever you had
last copied from a browser.

Found while fixing ADR 0060, and raised as a decision rather than fixed
there, because the obvious one-line version of it is wrong.

## Decision

### Deletes need a register, and the clipboard cannot be it

Making `d`/`x` write the system clipboard would fix `dd`+`p` and
introduce something worse: **every `x` would destroy what you last
copied from another application.** Deleting a character is editing, not
copying, and an editor that silently empties your clipboard as you edit
is not one people forgive.

So this adds the thing vim actually has and this editor was missing: an
**unnamed register**, internal to the editor, filled by `y`, `d`, `c`
and `x`, and read by `p`/`P`. The system clipboard becomes what it
should always have been — a separate destination, reached with `Ctrl+C`
/ `Ctrl+V`, which work in every mode.

### Yank mirrors to the clipboard; delete does not

The one asymmetry, and the only judgement call here. `y` is the explicit
"I want this text" gesture, so carrying it to other applications is
almost always what was meant, and that behaviour already existed and was
worth keeping. `d`, `c` and `x` are editing operations that happen to
produce text as a by-product. They fill the register and leave the
clipboard alone.

Real vim draws this line differently — nothing touches the system
clipboard unless you ask for `"+` or set `clipboard=unnamed` — but vim
has `"+y` to reach across, and this editor deliberately has no register
prefixes. Between "yank never leaves the editor" and "delete wipes your
clipboard", the middle position is the one that surprises nobody.

### `p` does not fall back to the clipboard

An empty register pastes nothing. It would be easy to make `p` fall back
to the system clipboard when nothing has been yanked yet, and that was
rejected: it would mean `p` quietly changing meaning the first time you
delete something, which is worse than a `p` that always means exactly
one thing. `Ctrl+V` pastes the clipboard, in any mode, including Normal.

### Process-wide, not per viewport

Each open buffer is its own `EditorViewport` (ADR 0054), so a register
stored on the viewport would make yanking in one file and pasting in
another — one of the most obvious uses of a register — silently paste
the wrong thing. `VimRegister::unnamed()` is a single instance for the
process. Two separate `ase_gui` processes have separate registers, which
is also how vim behaves.

### Linewise travels with the text

`linewise` is what separates `yy` from `yw`: it decides whether `p`
opens a new line below or inserts after the cursor. It is a property of
the stored text, so it lives in the register rather than in a viewport
member (`m_vimLastYankWasLinewise`, now gone — it was per-viewport, and
would have had the same cross-buffer bug).

Storing normalises a linewise payload to whole newline-terminated lines
whatever shape the range that produced it had. That matters because of
ADR 0060: a linewise delete at the end of the buffer takes the newline
*above* the lines, so the raw removed bytes are `\nfoo`, and a register
holding that would make `p` paste a blank line. One helper,
`vimSetRegister()`, so no call site has to remember.

## Consequences

Verified live, with a distinct string placed in the system clipboard by
another program first:

- `dd` on `bravo` then `p` on the line below moves the line — the idiom
  that previously did nothing.
- After that `dd`, the system clipboard still holds the other program's
  text, untouched.
- `yy` puts `alpha\n` on the system clipboard; the following `x` leaves
  it exactly as it was.
- `x` then `p` puts the deleted character back, charwise, even though
  the clipboard holds something else entirely — proof that `p` reads the
  register and not the clipboard.
- `yy` in one buffer then `p` in a second, freshly opened one pastes the
  yanked line: the register crosses buffers.
- `Ctrl+V` in Normal mode still pastes the system clipboard at the
  cursor.

The shortcut reference gained a line under the operators saying all four
fill the register, and a `Ctrl+V` row saying where the clipboard lives.

`ctest` 9/9, clean build, zero warnings.

Still deferred, and now cheap to add on top of this: **named registers**
(`"ayy`), the numbered ring (`"1p` for the last delete), and the
small-delete register. This is the one vim uses when you do not name
one, which is nearly always.
