# ADR 0054: Multiple buffers, and finally wiring the plugin host in

## Status

Accepted

## Context

Two items from [EXTENSIBILITY.md](../EXTENSIBILITY.md) and the post-v1
roadmap, taken together because the first is small and the second is the
largest functional gap the editor had: it could only ever hold **one file
per window**, with no way to open a second without losing the first.

## Decision

### A viewport per buffer, not a document per viewport

ADR 0052 predicted that multi-buffer would force extracting a `Document`
class (buffer + undo + syntax + path + LSP state) out of `EditorViewport`,
since the viewport owns all of those as singletons. On actually costing
it out, **that prediction was wrong, and the reverse is true.**

A `Document` earns its keep only when you need two *views of the same
file* — split view. That isn't this feature. Without that requirement,
"one `EditorViewport` per open buffer, stacked so one is visible" is not
a workaround for missing the extraction; it is the simpler correct model:
the viewport *is* the document view. Extracting `Document` first would
have meant moving fifteen-odd members and touching all nine translation
units to buy nothing today.

So: `QStackedWidget` of viewports, owned by `MainWindow`, which grew
from "a `closeEvent` override" into the thing that owns the buffer list.
`Document` is now explicitly parked until split view is wanted — at
which point it becomes the right call, for a reason that actually
applies.

The honest cost of this shape: some per-viewport state is duplicated
N times (a blink timer, a config-reload timer, a full set of floating
panels). Panels are inert until shown and hidden widgets get no paint
events, so the cost is small — but it is real, and it is the price of
not having a `Document`.

### The buffer bar is a dot, a name, and nothing else

Explicit design direction: filename with a dot beside it, active at full
opacity, inactive dimmed (dot and name together), **no lines above,
below or between**, close mark only on the active entry, minimal and
fast.

Deliberately *not* a `QTabBar`. Every native tab widget draws frames,
separators, a selected-tab lip and a hover plate — none of which this app
has anywhere else. The chrome language here is "no lines, state carried
by opacity" (ADR 0007's one-font-color pillar, ADR 0022's panels), and a
`QTabBar` would have imported a different one. `BufferBar` is ~180 lines
of custom paint instead, using the same opacity tiers the gutter already
uses to say "this line is yours / these are context".

Two consequences worth naming:

- **One buffer means no bar at all.** `sizeHint()` returns zero height
  below two entries — the filename is already in the window title, so a
  single-entry bar would be chrome that tells you nothing. Anyone who
  never opens a second file sees exactly the editor they had before this
  feature existed.
- **The close mark only exists on the active entry**, per the direction.
  An x on every buffer is five things asking to be clicked instead of
  one. Its hit rect is deliberately padded well beyond the 7px glyph.

The one thing added beyond the brief: a small opacity lift on hover for
inactive entries, so a click target acknowledges the pointer. It adds no
chrome — it's the same opacity channel, one tier up.

### Deliberately not shown in the bar: dirty state

The dot tracks *active*, exactly as specified, not *modified*. That is a
real trade: with three buffers open and two edited, the bar won't tell
you which. The dirty marker lives in the status bar and window title
(ADR 0023) and those follow the active buffer only. Flagged here as a
known gap rather than silently "improved", since the dot is the obvious
place to put it if it's wanted later.

### LSP starts on first activation, not at construction

Opening ten files must not spawn ten `clangd` processes for the nine you
never looked at, so `startLspClientIfConfigured()` moved out of the
constructor into `onActivated()`, called when a buffer first becomes
visible.

It deliberately does **not** stop on switch-away: restarting clangd costs
a full reindex, and switching buffers is the common action — making the
common action slow to save memory on the uncommon one is the wrong
trade. So the cost is one server per *visited* C file. The real fix is
one project-wide server handling several `didOpen` documents, which is
what LSP is designed for; that's now a named roadmap item.

### `openFile()` deleted

In-place replacement of a viewport's contents is obsolete once opening a
file means opening a buffer. `FileBrowserPanel` now emits through
`EditorViewport::fileOpenRequested` and the window decides; the 52-line
`openFile()` and its LSP-restart dance are gone. Reopening an
already-open path switches to it rather than creating a second copy.

### Plugin host: wired, with the undo hazard handled honestly

`EditorViewport` creates an `AsePluginHost`, loads `<config dir>/plugins/`
at startup (a missing directory is not an error — same "unconfigured is
a normal state" stance as `lsp_command`/`build_command`), and
`runCommand()` falls through to the registry for any `:name` that isn't a
built-in. Built-ins are checked first, so a plugin cannot shadow one.

The hazard, and why it is handled the way it is: a plugin command is
handed the raw `AseBuffer` and edits it directly, because the ABI
(ADR 0009) has no way to do anything else. That leaves every offset
already recorded in the undo stack potentially stale — and undoing
against stale offsets *corrupts the buffer* rather than merely doing the
wrong thing. So the undo history is **dropped** after a successful
plugin command. Losing history is a visible, understandable cost; silent
corruption is not. Routing plugin edits through undo needs the wider
plugin context described in EXTENSIBILITY.md, and this is the concrete
reason that widening is worth doing.

## Consequences

Verified live end to end: three buffers open showing `• alpha.c`
`• beta.c` `• gamma.c ×` with only the active one at full opacity and no
rules anywhere; click-to-switch and `Ctrl+Tab` both move the active entry
and its close mark; closing back down to one collapses the bar to zero
height; a dirty buffer refuses to close without the themed confirmation,
and Cancel leaves the unsaved edit intact. The plugin host was verified
with a real Lua plugin (`:shout`, uppercasing the buffer) loaded from
`~/.config/ase/plugins/` — syntax, diagnostics and the dirty marker all
updated correctly afterwards.

Every confirmation in the window now goes through one themed
`confirmDiscard()` rather than each building its own box — the
per-buffer close was briefly a native `QMessageBox::question`, which is
exactly the mismatch ADR 0044 already had to fix once.

New keys: `Ctrl+Tab` / `Ctrl+Shift+Tab` cycle buffers, `Ctrl+W` closes
one. They are window-level `QShortcut`s rather than additions to
`EditorViewport`'s Ctrl-chain, because they act on the window's buffer
list, not on text — and Qt dispatches shortcuts before the focus
widget's key handler, so `Ctrl+Tab` never reaches the viewport's Tab
case.

`ctest` 9/9, clean build, zero warnings.
