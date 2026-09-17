# ADR 0119: A command belongs to its buffer

## Status

Accepted

## Context

Reported as "after clicking a reference I cannot get back to the panel
with the keyboard". The cause was larger than the symptom.

ADR 0113 gave the window one `CommandRegistry` and had each viewport
register into it, skipping names already there:

```cpp
if (!registry->contains(name)) registry->add(name, description, run);
```

Every `editor.*` lambda captures the viewport it was made from. With one
buffer that is invisible. With two, the first buffer opened owns every
editor command for the rest of the session.

So `Ctrl+S` with two files open saved the *first* one — and said nothing,
because that buffer was usually clean and saving a clean buffer writes
nothing. The edit stayed unsaved with its dirty marker showing while the
keystroke that should have saved it reported success by being silent.
The same went for undo, find, copy and the rest.

The reported symptom was the mild end of it: after a jump, the panel
commands ran against the viewport the user had left.

## Decision

Two tables, split by what a command acts on.

The window's registry keeps what the window owns: the buffer list, the
jumplist, the output panel's size. One of each exists, so one lambda is
correct.

Each viewport keeps its own registry for everything that acts on a
buffer. A lambda capturing `this` is then right by construction, because
it only ever runs for the buffer it was made from.

`handleBoundChord` tries the viewport's table, then the window's. A name
in both would mean a viewport shadowing the window; none does, and the
`editor.*` / `buffer.*` split keeps it that way.

## Consequences

Verified the way the bug was found rather than by reading: two files
open, type into the second, press `Ctrl+S`, and read both files off
disk. Before, the second file's text was still only in memory. After, it
is on disk and the first file is untouched.

The bug was invisible in every test that existed because they all used
one buffer, and invisible in manual use for the same reason until a
second file was opened — which find-references does constantly, by
design. A feature that opens files for you is what made a latent bug in
another feature reachable.

`keys::problems()` now has to see both tables, or every `editor.*`
binding in a config file would be reported as naming nothing.
