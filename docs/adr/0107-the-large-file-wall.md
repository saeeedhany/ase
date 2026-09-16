# ADR 0107: The large-file wall

## Status

Accepted

## Context

An 8MB file was opened in the editor to see what would break. Typing
`hello` landed the `h` and lost the rest, and Escape never registered.

The first guess was `refreshCache()`, which copies the whole buffer and
rescans it for line starts on every keystroke. Measured, that is 1.9ms
on 8MB — not it. The cost is Tree-sitter, which has to parse the whole
file to build a tree however little of it is on screen. The query is
windowed to the visible region (ADR 0072); the parse cannot be.

Measured on real C — this repository's own sources concatenated, so the
nesting is representative rather than a flat list of declarations:

| file | cold parse | one keystroke |
|---|---|---|
| 113 KB | 25 ms | 3 ms |
| 1 MB | 230 ms | ~8 ms |
| 4.5 MB | 1070 ms | 20 ms |

Both numbers land on the UI thread: the cold parse inside the
constructor, the incremental one inside `refreshCache()` on every edit.

A synthetic 8MB file of 400,000 flat declarations is far worse — 1744 ms
and 280 ms — because a very flat tree is the bad case for an incremental
reparse. That file is what dropped the keystrokes. Real code at the same
size does not; it stays about 20 ms per keystroke, which is bad but not
broken. The synthetic number is the honest worst case and the real one
is the honest typical case, and they are an order of magnitude apart.

## Decision

### The re-highlight waits for a pause in typing, past 256 KB

Under the threshold the parse runs on the keystroke as before, so
colours never lag on an ordinary file. Over it, a single-shot 40 ms
timer is restarted by each edit and the parse runs when typing stops.
The keystroke cost stops scaling with the file.

### Highlighting is skipped past `syntax_max_kb`, default 1024

The debounce fixes typing but not opening, which is one unavoidable
parse. At the 1 MB default that is about 230 ms; at 4.5 MB it would be
just over a second, which is a visible freeze on a file the user has not
even looked at yet.

This is a real trade and not a free win: past the cap the file opens
instantly and has no colours at all, where before it opened in a second
and had them. Files that large are almost always generated or
amalgamated, where opening fast matters more than syntax colour, and
anyone who would rather wait can raise the key or set it to 0.

The status bar says `no highlight (large file)`. A missing colour with
no explanation reads as a broken grammar, which is the same silent
failure the LSP gate was criticised for in ADR 0086.

## Consequences

Sanitizers had never been wired to the GUI — 10,614 of the project's
14,187 lines. They are now, and they immediately caught a bug in this
very change: the highlight timer was created at the end of the
constructor, while `refreshCache()` runs partway through it and arms the
timer. That is a null dereference on any file over the threshold, and it
is the second timer-initialisation crash in this project.

The cap is checked once, when the syntax handle is built. Typing past it
in a file that started under it keeps its colours rather than losing
them mid-edit.

What this does not do is make the parse concurrent. That is the fix that
would give an instant open *and* colours, and it is the right eventual
answer; it needs the parse to own a copy of the text on a worker thread
and to version its results so a stale one is discarded. The cap is the
cheap correct answer until then, not a substitute for it.
