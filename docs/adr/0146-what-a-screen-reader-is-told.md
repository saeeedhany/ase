# ADR 0146: What a screen reader is told

## Status

Accepted

## Context

The editing area is a `QWidget` that paints its own text. Nothing about
it is discoverable the way a `QTextEdit`'s is: to an assistive
technology it was a rectangle with the name "Editor" and no contents,
no caret, no selection, no lines.

[ADR 0012](0012-polish-phase-scope.md) recorded the gap and ROADMAP
carried it as the one open accessibility item. It is the only entry on
that list that is an outright exclusion rather than a rough edge —
someone using a screen reader could not use this editor at all, not
awkwardly, at all.

It stayed open because it looked unverifiable here. That turned out to
be half true and worth separating carefully.

## Decision

`EditorAccessible`, a `QAccessibleWidget` implementing
`QAccessibleTextInterface`, installed through a factory from the first
viewport constructed so tests get it without a `main()`.

The role is `EditableText`; the state carries `editable`, `multiLine`
and `selectableText`; the name is the file being edited, so moving
between buffers is audible, and an unnamed buffer says "Untitled"
rather than announcing an empty string.

### Offsets are characters, and the editor counts bytes

Everything inside `EditorViewport` is a UTF-8 byte offset. Everything an
assistive technology asks about is a `QString` index. They agree exactly
as long as a file is ASCII and diverge the moment it is not.

A reader given the wrong offset is worse than one given nothing: it
reads a character that is not there, or splits one in half. So the
conversion lives in one place, `editor_viewport_a11y.cpp`, and the
public surface is named for it. The test uses `héllo wörld`, where
character 7 is byte 8.

### A line is read without its newline

`textAtOffset(LineBoundary)` returns the line's text, not the break
after it, because a reader announcing a line should not announce the
break as part of it.

That has a consequence worth writing down, because it is the one thing
here I got wrong first: a line's *end* offset is the newline's own
position, and the newline still belongs to the line before it. So
`textAfterOffset` landing on `end` asks about the line it just came
from. A *word's* end is already the next word's start, so stepping again
there would skip one. The step adapts — it moves on, and only if the
answer is the same unit does it move once more.

Words and sentences come from `QTextBoundaryFinder` rather than the
editor's own `isWordChar()`. Vim's idea of a word is deliberately
narrower than the platform's, and a reader announcing "words" should
match what the rest of the desktop calls one.

### Being told, not only asked

An interface that only answers questions leaves a reader with a stale
snapshot. `refreshCache()` computes the smallest edit explaining the
difference — common prefix, common suffix, what is left — and sends
`TextInsert`, `TextRemove` or `TextUpdate`; caret moves send
`TextCaretMoved`.

**None of it runs unless something is listening.** `QAccessible::isActive()`
is false until an assistive technology attaches, and the diff walks the
whole buffer, so it must not be on the keystroke path for everyone
else. Capturing the old text costs a refcount bump rather than a copy,
because `QByteArray` is implicitly shared and the `resize()` below is
what detaches.

`isActive()` is not a flag, though — it asks the platform accessibility
plugin, and `setActive()` writes through to the same place. On a machine
with no such plugin it answers false forever and cannot be persuaded
otherwise, which is every CI runner here. The notification tests passed
locally and failed on all three GUI jobs for exactly that reason, and
the fix is an explicit override rather than a test that only means
something on one machine. The tests now run with `isActive()` false on
purpose, so they assert the same thing everywhere.

## Consequences

Seventeen tests, through `QAccessible::queryAccessibleInterface()` —
the path an assistive technology actually takes — and through
`QAccessible::installUpdateHandler()`, which is the same seam a platform
bridge uses to receive events. Disabling the notifications fails three
of them; the interface tests cover role, state, name, value, caret,
selection, character/word/line boundaries, both ends of the buffer,
character rectangles, and the point-to-offset mapping.

**What this does not prove.** No Qt AT-SPI bridge plugin is installed on
this machine — checked, rather than assumed, after saying so twice — so
Qt cannot publish the tree to a real client here and nothing was heard
out loud. Every answer is verified; how Orca or NVDA narrates them is
not. That last mile needs a person at a desktop with a screen reader
running, and it should happen before anyone claims this editor is
accessible.

What is certainly still missing: the buffer bar, the status line and
the floating panels have names but no structure, and there is no
announcement when a panel opens or the mode changes. The editing area
was the part that made the application unusable rather than awkward,
and it is the part this covers.
