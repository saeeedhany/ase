# ADR 0020: Clipboard — cut/copy/paste on top of the selection model

## Status

Accepted

## Context

Phase 11 (ADR 0019) gave every cursor an optional selection range.
Cut/copy need a selection to operate on, so this phase sits directly
on top: `hasSelectionAt`/`selectionMinAt`/`selectionMaxAt`/
`deleteSelectionAt` are reused as-is, no changes to that layer.

## Decision

Three new `EditorViewport` methods, wired into `keyPressEvent`'s
existing Ctrl-modifier branch alongside `S`/`Q`/`D`/`Z`:

- **`copySelection()`** (`Ctrl+C`): for every cursor with an active
  selection, reads the range straight out of `m_cache` (already a full,
  current mirror of the buffer, per ADR 0006 — no need for
  `ase_buffer_get_text`). Multiple selections join with `\n`, the
  standard multi-cursor copy convention. Sets the result via
  `QGuiApplication::clipboard()->setText(...)`. No selection anywhere →
  no-op, clipboard untouched (documented decision, matching the
  project's existing "don't guess" pattern already used for
  `build_command`/`lsp_command` elsewhere in the plan — no implicit
  whole-line copy fallback). Returns `bool` so `cutSelection` can reuse
  it directly as its "is there anything to do" check.
- **`cutSelection()`** (`Ctrl+X`): `copySelection()` first, then (only
  if it returned `true`) deletes every selection as **one undo group**
  — the same `begin_group`/`deleteSelectionAt` loop/`end_group` shape
  every other multi-cursor edit in this file already uses, so cut
  undoes as a single action for free.
- **`pasteClipboard()`** (`Ctrl+V`): reads
  `QGuiApplication::clipboard()->text()`, and if non-empty, hands it to
  the existing `insertText()` unchanged — which already replaces each
  cursor's active selection first (ADR 0019) and already wraps the
  whole multi-cursor broadcast in one undo group. Deliberately inserts
  the *same* text at every cursor rather than distributing clipboard
  lines one-per-cursor (a real feature some editors have, e.g. pasting
  N copied lines onto N cursors one each) — considered and declined for
  v1, added complexity the "minimal" pillar doesn't need yet.
- No `CMakeLists.txt` change: `Qt6::Widgets` already transitively links
  `Qt6::Gui`, which is where `QClipboard`/`QGuiApplication::clipboard()`
  live — confirmed by a clean build after just adding the `#include`s.

## Consequences

Verified live against the running `ase_gui`, driven with `xdotool`
under the real X session, checked against the actual system clipboard
via `xclip` (a process independent of the Qt app, so this proves the
data really left the process, not just an in-memory `QString`):
- `Ctrl+C` on a 5-character selection → `xclip -selection clipboard -o`
  reads back the exact text.
- `Ctrl+V` — seeded the system clipboard independently via `xclip
  -selection clipboard` (decoupled from any Qt process, since X11's
  clipboard has no backing store of its own: killing the owning
  process without a clipboard manager running drops the selection
  immediately — a real X11 property, not an editor bug, and the reason
  this test seeds the clipboard externally rather than chaining off a
  same-session `Ctrl+C`) → pasted text landed exactly at the cursor,
  confirmed via a save-to-file round trip.
- `Ctrl+X` removed the selection, the clipboard held exactly the cut
  text, and one `Ctrl+Z` restored the original buffer content as a
  single action.
- Multi-cursor copy: two simultaneous cursor selections (`Alt`+click to
  add a second point cursor, then `Shift+Right` — which extends *every*
  cursor's selection in the same keystroke, since the move primitives
  already loop over all of `m_cursors`) produced a clipboard value with
  the two ranges joined by `\n`, in buffer order.

Not built, deliberately: a system clipboard manager isn't this editor's
responsibility (same reasoning as declining an implicit copy
fallback) — pasting after the source app closes without one running is
an X11-wide limitation, not something `ase_gui` can or should work
around. Find/replace (Phase 13) is next, unrelated to this phase's
additions.
