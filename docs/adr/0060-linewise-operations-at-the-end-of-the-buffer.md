# ADR 0060: Linewise operations at the end of the buffer

## Status

Accepted

## Context

Reported from use: `dd` on the last line of a file does nothing at all.
It only starts working one line up.

## Decision

### One asymmetry, four symptoms

Every linewise operation built its byte range as "from the start of the
first line, through the start of the line *after* the last one" — the
trailing newline goes with the text, so no blank line is left behind.
That is right everywhere except at the end of the buffer, where **there
is no line after and no trailing newline to take.**

The range there was clamped to the end of the buffer instead, which
produces four different wrong behaviours depending on what the last line
is:

- **The last line is empty** (any file ending in a newline — i.e. almost
  all of them). Its start offset already *is* the end of the buffer, so
  the range was zero-length and `dd` deleted nothing. This is what was
  reported, and why it looked like the last line was special-cased out.
- **The last line has text.** `dd` removed the text but not the line
  break above it, emptying the line rather than deleting it.
- **`yy` on the last line** yanked a string with no trailing newline —
  nothing at all on an empty last line — so the register held a fragment
  and the following `p` pasted a fragment, or nothing.
- **Linewise Visual `d`** on the last line had the same shape as `dd`,
  through a different code path (a selection range rather than a line
  count), and left an empty line behind.

### The rule

A linewise range that ends at the end of the buffer takes the
**preceding** newline instead of a following one. That is what removes a
line rather than emptying it, and it is what real vim does — which is
why deleting the last line moves the cursor up.

It lives in one place, `vimLinewiseDeleteStart()`, used by both `dd` and
linewise Visual `d`, because the same mistake made independently in two
code paths is how it survived this long. The first line is exempt:
deleting from there legitimately takes the whole buffer.

Yanks get the mirror of it — a linewise register always ends in a
newline, appended if the buffer had none to give.

`c` is deliberately **not** extended over the preceding newline. `d`
removes lines; `c` leaves you typing where they were, and swallowing the
break above would drop the insertion point onto the end of the previous
line.

### Two smaller end-of-buffer fixes found while verifying

- Linewise `p` on the last line of a file that **does not end in a
  newline** appended the register directly to the end, so `a\nb` + `p`
  produced `a\nbb` — the pasted line joined onto the last one. The paste
  now trades the register's trailing newline for a leading one, so the
  buffer gains exactly one line.
- A linewise Visual delete left the cursor on the newline the range was
  extended over, i.e. visually past the end of the previous line. It now
  lands on that line's first non-blank, like every other linewise
  operation in Vim mode.

## Consequences

Verified live, on a file ending in three blank lines:

- `G` then `dd` deletes the last (empty) line and lands on the new last
  line; repeating walks up the file one line per press, where before it
  did nothing until two lines from the end.
- `dd` on a non-empty last line removes it outright, leaving no blank —
  `one`/`two` + `dd` on `two` leaves exactly `one`.
- `dd` on the only line empties the buffer; one `u` restores the entire
  original file, so the extended range is still a single undo group.
- `yy` on the empty last line now puts `\n` in the register (confirmed at
  the clipboard) and `p` adds a line; before, both were no-ops.
- On a file with no trailing newline, `yy`+`p` on the last line gives
  three lines, not a joined one.
- Linewise Visual `d` on the last line leaves no blank line and puts the
  cursor on the first non-blank above.
- Unchanged mid-file: `dd` on line 2 of four leaves the rest intact, and
  `V j d` from the top removes exactly two lines.

`ctest` 9/9, clean build, zero warnings.

**Found, not fixed, and deliberately left for a decision:** `d`/`dd`/`x`
do not put the deleted text in any register, so vim's most common
line-move idiom, `dd` then `p`, does not work — `p` pastes whatever the
system clipboard happened to hold. ADR 0046 scoped Phase 1 to "the
clipboard is the only register", and the missing half of that is that
deletes never fill it. Making them fill it is a real design choice, not a
bug fix: the register here *is* the system clipboard, so every `x` would
destroy whatever you last copied from another application. The proper
answer is vim's own model — an internal unnamed register for `d`/`y`/`p`,
with the system clipboard reachable separately — which is a larger piece
of work than this ADR.
