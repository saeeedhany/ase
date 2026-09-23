# ADR 0138: Two paper cuts

## Status

Accepted

## Context

ROADMAP keeps a list of things too small to schedule and too real to
close. Two of them had been on it long enough to be worth doing rather
than re-reading: `Ctrl+D` not wrapping, and the buffer bar not eliding.

Neither is a feature. Both are cases where the editor does something
defensible and slightly wrong, every time, forever.

## Decision

### `Ctrl+D` wraps

"Add a caret at the next occurrence" searched forward and stopped at the
end of the buffer. So the command's usefulness depended on where in the
file you happened to start: the same keystroke found every use of a name
declared at the top, and none of one declared at the bottom. Renaming a
local in a function near the end of a file was the case that made it
obvious.

Two passes now — forward from the last caret, then from the top up to
the word it started from.

**An occurrence that already has a caret is skipped rather than
re-added.** `normalizeCursors()` would drop the duplicate, so adding it
would look like the key had done nothing, while occurrences were still
left to take.

Deciding whether an occurrence is taken turned out to be the only
subtle part. The obvious test — is there a caret at the word's end —
is wrong, because it is only true for the carets *this command* adds.
The one you started from is wherever you left it, usually mid-word, so
the first wrap re-added the caret's own word. The test is whether any
caret lies anywhere in `[start, end]`.

### The buffer bar elides

A long filename made its tab as wide as the name, pushing the others out
of the strip. Names are shortened to about 22 characters of the current
font, **middle**-elided: both ends of a filename carry meaning, and
eliding the tail turns `editor_viewport_render.cpp` and
`editor_viewport_input.cpp` into the same tab.

The shortening happens in `relayout()` rather than in `paintEvent()`, so
the width a tab reserves is the width it draws. Doing it at paint time
would have left every long tab full-width with a short name in it.

The limit is in pixels, measured through `QFontMetrics`, so it follows
the configured font size instead of a character count that means
something different at every size.

## Consequences

Thirteen tests, in two suites. `elideTabName()` and `maxTabNameWidth()`
are free functions in a `bufferbar` namespace precisely so the first
can be tested without constructing a widget; the strip's actual drawing
is still checked by looking at it.

The multi-cursor tests needed `cursorCount()` and `cursorOffsets()` on
`EditorViewport` — the caret set had no public shape to assert on, which
is the same reason this behaviour was wrong for as long as it was. They
also have to call `registerCommands()` first: a bare viewport has no
command registry, so `runCommandByName()` quietly returns false, which
cost a debugging pass.

Both changes were verified by reverting them — one pass instead of two
fails two of the wrap tests.

Still on the list: vertical multi-cursor movement has no per-cursor
sticky column, and the highlight capture styles remain hardcoded.
