# ADR 0055: The dot means unsaved, plus new-file and panel fixes

## Status

Accepted

## Context

Five pieces of feedback after using the multi-buffer work from ADR 0054,
all of them corrections to decisions made one round earlier.

## Decision

### The dot marks unsaved changes, not the active buffer

ADR 0054 put a dot beside every filename, sharing the name's opacity, as
a purely decorative mark. That was wrong, and the reason is worth
recording: **opacity was already saying "this is the active buffer", so
the dot said nothing a second time.** Meanwhile the one thing you
genuinely cannot see about a buffer you are *not* looking at — whether
it has unsaved edits — had no indicator at all. The dirty marker lived
only in the status bar and window title, both of which follow the active
buffer.

So the dot now appears only on buffers with unsaved changes, and is
deliberately exempt from the active/inactive dimming
(`kDirtyDotMinAlpha`): warning you about a file you are not looking at
is its entire purpose, so it must stay readable on a dimmed entry.

Its slot is reserved whether or not it is drawn, so names do not shift
sideways the instant a file becomes dirty — a bar that reflows on your
first keystroke would be worse than a little extra left padding.

This supersedes ADR 0054's dot decision, and closes the "show unsaved
state in the buffer bar" gap that ADR flagged as deliberate.

### `Ctrl+N` creates a new file

An empty, pathless buffer. Nothing else was needed: `save()` already
routes an empty path to Save-As (ADR 0006), and the bar already labels
a pathless buffer `untitled`.

### The Open/Save-As panel behaves like a file dialog now

Three real defects, in rough order of severity:

1. **Save-As silently overwrote an existing file.** No confirmation, no
   warning. That is the one genuinely destructive thing this panel could
   do, and it did it without asking. Now a themed confirm (matching the
   window's own, per ADR 0044 — not a native dialog).
2. **A typed path worked in Save-As and did nothing in Open.** Open
   treated the field purely as a filter against the current directory,
   so typing a real path filtered the list to nothing and Enter did
   something unrelated. This asymmetry is the single biggest reason the
   panel felt unfamiliar. Both modes now resolve a typed path — absolute,
   `~`-prefixed, or containing a separator — navigating to it if it is a
   directory, otherwise opening/saving it. In Open, a path that does not
   exist yet is accepted, since opening a new file by name is a normal
   editor action (ADR 0006) and the panel was the one place that refused
   to do it.
3. **Neither showed which directory you were in.** The directory name was
   a placeholder in the input, which disappeared the moment you typed —
   so exactly while filtering, the context you needed was gone. There is
   now a dim path line under the input, with `$HOME` collapsed to `~`.

Also: `~` expansion, and Enter on an unmatched filter in Open now treats
what you typed as a new file name in the current directory instead of
doing nothing.

### About panel: logo in its own static column, text left-aligned

Was a centered logo with centered prose stacked beneath it (ADR 0028,
which chose centered text specifically because left-aligned text under a
centered image looked unaligned). The fix is not to re-align the text
under the image but to stop stacking them: the logo now holds a fixed
132px column pinned to the top, with the text left-aligned beside it.

Centered prose is harder to read — every line starts at a different x, so
the eye has to re-find the left edge on each one. Side by side gives the
text one hard left edge to track and still gives the logo its presence.
The logo is `Fixed`/`AlignTop` so it cannot stretch or drift downward as
the text beside it grows.

Secondary lines (version, byline, licence) drop one opacity tier — the
same "vary opacity, never hue" move as the gutter and comments
(ADR 0007).

### Keyboard shortcuts: a data table, and content that is actually true

The panel was one long HTML string literal, and it had **drifted badly out
of date** — no Vim mode, no buffers, no font zoom, no `:` command line,
no plugin commands, despite all of those shipping. That is not an
accident: adding a row meant hand-writing four tags, and that friction is
what stops documentation being updated.

It is now a `QVector<HelpSection>` of `{keys, description}` rows rendered
by `buildHelpHtml()`. Sections that require opting in carry a dim note
(`Vim mode — vim_mode = true`) rather than leaving someone hunting for a
key that does nothing on their setup. Keys sit in a fixed-width column
one opacity tier down, descriptions at full strength, so it reads as two
aligned columns rather than ragged pairs.

Both panels blend their dim colour to a solid RGB rather than passing
`#AARRGGBB`: **Qt's rich-text CSS does not reliably parse an alpha
channel in a hex colour**, so the dimming would have silently rendered
fully opaque.

## Consequences

Verified live: `alpha.c` active and clean shows no dot while `• beta.c`
sits dimmed with its unsaved dot clearly readable — the exact case the
old design could not express. `Ctrl+N` opens `untitled`. Typing an
absolute path into Open now opens it as a buffer (previously a no-op).
Save-As onto an existing file raises the themed overwrite confirm. The
About panel renders as two columns with the logo static on the left, and
the shortcuts panel lists `Ctrl+N`, buffer switching, font zoom and the
full Vim command set.

`ctest` 9/9, clean build, zero warnings.

Still not shown anywhere: which buffer a dirty dot belongs to when the
bar overflows the window width — the bar does not scroll or elide yet.
Fine at a handful of buffers, a real gap at twenty.
