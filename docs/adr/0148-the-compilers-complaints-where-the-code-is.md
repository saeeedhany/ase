# ADR 0148: The compiler's complaints, where the code is

## Status

Accepted

## Context

[ADR 0147](0147-working-out-how-to-build.md) made `:compile` work
without configuration, and closed by naming what it did not do: the
build's output was text in a panel. An error told you a file, a line and
a column, and you read them and went there yourself — while the editor
already had a way to mark a line, because the language server has used
one since [ADR 0029](0029-lsp-diagnostics-wiring.md).

## Decision

The output is parsed back into diagnostics and drawn the same way the
server's are.

`ase_build_parse_output()` in core turns text into a list of file, line,
column, severity and message. Two shapes cover gcc, clang and MSVC:

```
path/file.c:12:5: error: message
path\file.c(12,5): error C2065: message
```

The column is optional in both. Anything that does not match is not a
diagnostic, which is nearly every line a build prints.

### Left to right, and not after a space

Both details are bugs the tests found rather than decisions made up
front.

Scanning for the separator **from the right** parses `a.c:12:5:` as line
5 of a file called `a.c:12`. From the left it is unambiguous, and the
Windows drive letter that motivated the right-to-left scan is handled
anyway: everything after the separator is validated, so `C:` simply
fails and the scan moves on to the `(` that follows.

A compiler leaves no space between the path and the line number, and
`strtol` skips leading whitespace, so without that check `Time: 12:05:33`
starts to look like a diagnostic. It is rejected a step later for having
no severity word, but only by luck, and luck is not a parser.

### Two producers, one list

Diagnostics carry the source they came from. A `didChange` arrives on
every keystroke and republishes the server's whole set; a build's
findings are not the server's to withdraw. Each producer now replaces
only its own, so neither clears the other.

### A point becomes a range on this side

A compiler names a position. Only the buffer knows how long that line
is, so the mark is extended to the end of the line here rather than
guessed at by the producer.

A diagnostic for a line the file no longer has is **dropped**, not
clamped onto whatever is nearest. A mark on the wrong line is worse than
no mark.

### Only into buffers that are already open

A build can report on files you are not looking at. The obvious
`viewportForPath()` opens a file to put a warning in it, and a warning
is not a reason to open anything. Findings go to the buffers already
open; the rest stay readable in the panel.

### They stay until the next build

A build's marks are not cleared when you start editing, so fixing one
error leaves the others where they were — and after enough edits they
point at the wrong lines.

Kept anyway, because the alternative is losing the list the moment you
start acting on it, and because this is what vim's quickfix does and
what this editor's users will expect. It is drift, and it is worth
knowing about.

## Consequences

Sixteen tests in core over the parsing — both compiler dialects, a
message containing colons, `fatal error`, ordinary build chatter
including `make: *** [Makefile:7: all] Error 1`, empty and malformed
input — and seven in the GUI over the half that needs a buffer: a point
becoming a range against the real line length, a line past the end being
dropped, a build replacing only its own, severity surviving, and an
empty line still getting a mark.

Verified in the editor against a file with two errors and a warning in
it: three gutter marks and three underlines, on the right lines, in the
right colours.

Not covered by a test: that a server's republish leaves a build's marks
alone. The predicate is one line and symmetric with the direction that
*is* tested, but setting an LSP URI on a viewport needs a running server,
and a test that faked it would only be checking itself.

What this still does not do is let you walk the list. Vim has `:cn`, and
the marks are in the gutter rather than anywhere you can step through.
That is the obvious next thing and is deliberately not here.
