# ADR 0070: The jumplist

## Status

Accepted

## Context

Go-to-definition (ADR 0067) made the gap impossible to ignore: you can
follow a name into a file you did not choose, and the only way back is
the buffer bar. ADR 0068 cleared `Ctrl+O` and `Ctrl+I` for this.

## Decision

### The shape is the undo stack's

A list plus an index into it. Recording a jump truncates everything
after the index and appends — exactly the rule a new edit follows
against the redo stack. `m_jumpIndex == m_jumps.size()` means "at the
present", with nothing to go forward to.

It lives in `MainWindow`, not the viewport: jumps cross buffers, and the
buffer list is the window's. The same split that already routes search
hits and cross-file definitions through the window.

### What counts as a jump

The one rule worth stating plainly: **a jump is a movement you could not
have made with `h j k l`.** Record too much and "back" takes you one
line up; record too little and the key does nothing when you need it.

Recorded: `gg`, `G`, `:N`, `{`, `}`, go-to-definition, following a
search hit, and opening a file (from the browser or `Ctrl+P` — the most
common way to end up somewhere you want back from).

Not recorded: `h j k l w b e f t`, scrolling, `Ctrl+D`/`Ctrl+U`.

Three subtleties the sites have to get right:

- **`gg`/`G` with an operator pending is a range, not a move** —
  `dG` deletes to the end of the file; it does not take you there. Those
  record nothing.
- **Search records once, when the bar opens**, not on each incremental
  keystroke. Otherwise typing a six-character query buries the position
  you actually wanted under six entries.
- **Go-to-definition records when the answer arrives**, not when the
  request is sent, so a lookup that finds nothing leaves no phantom
  entry.

### Going back records the present

The first `Ctrl+O` from the present pushes where you are before moving,
or forward has nowhere to return to and back becomes a one-way door.
Vim does the same. Subsequent presses don't re-push, because the index
is no longer at the end.

Consecutive entries on the same line of the same buffer are dropped:
without that, `gd` twice on one symbol records two identical positions
and the first `Ctrl+O` appears to do nothing.

### Entries outlive their buffers

An entry carries both a path and the viewport's address. The address
finds a buffer that is still open; the **path reopens one that was
closed**, which is what vim does and what makes the history trustworthy
over a long session.

An untitled buffer has no path, so a closed one cannot come back. That
entry is dropped when it is reached, with a message, rather than left as
a step that silently does nothing every time you pass it.

### Two bindings again

`Ctrl+O`/`Ctrl+I` are vim's. `Alt+Left`/`Alt+Right` are what every
browser and IDE uses. The same "two audiences" split as `gd`/`F12`, and
two lines of code.

They are window shortcuts rather than viewport keys, like `Ctrl+Tab` and
`Ctrl+W` — the list spans buffers, so it belongs to the object that owns
them.

## Consequences

Verified live:

- `G` to line 888, `Ctrl+O` back to line 1, `Ctrl+I` forward to 888.
- `Ctrl+P` opens `motion.h`; `Ctrl+O` returns to `main.cpp` **at line
  888**, the exact position, not the top of the file.
- Pressing `Ctrl+O` past the beginning says `no earlier position`;
  `Ctrl+I` past the end says `no later position` — the message line
  (ADR 0062) again turning a dead key into an answer.
- Closing `motion.h` and pressing `Ctrl+I` **reopens it** and lands on
  the remembered line.

`ctest` 9/9, clean build, zero warnings.

Deliberately not recorded, and arguable: **switching buffers by hand**
(`Ctrl+Tab`, clicking a tab). Vim counts a buffer switch as a jump. Here
the tab strip is always visible and switching back is one click, so the
history stays about *where you were reading* rather than every tab you
have touched. Easy to change if it feels wrong in use.

Also not done: `''` (back to the last position in this file), marks, and
showing the list — `:jumps` in vim. The list exists and nothing displays
it, which is fine until someone wants to know why back went where it
did.
