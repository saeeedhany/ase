# ADR 0019: Selection model — a per-cursor anchor, not a separate range type

## Status

Accepted

## Context

Every prior phase gave `EditorViewport` point cursors only (`m_cursors`,
ADR 0012) — no selection/anchor concept at all, and no
`mouseMoveEvent`/`mouseReleaseEvent` handling. Clipboard, find/replace,
and typing-over-a-selection (this pass's next phases) all need a real
selection to operate on; building it once, correctly, avoids three
half-selection-aware features later.

## Decision

- **New state, index-aligned with `m_cursors`**: `QVector<size_t>
  m_selectionAnchors`, same size as `m_cursors` always.
  `m_selectionAnchors[i] == m_cursors[i]` means cursor `i` has no active
  selection; otherwise the range is `[min, max)` of the pair. This
  reuses the exact pattern `m_renderedCaretPos` already established
  (ADR 0015) rather than inventing a separate `Selection` struct or a
  parallel single-selection field that would need its own
  multi-cursor-awareness.
- **Single-cursor primitives take an index, not a reference.** Every
  `insertTextAt`/`deleteBackwardAt`/`deleteForwardAt`/`moveCursor*At`
  changed from `size_t &cursor` to `int i`, since each now needs to
  reach both `m_cursors[i]` and `m_selectionAnchors[i]`. No external
  callers exist (all are private, called only from their batch-loop
  counterpart in this file), so the signature change is self-contained.
- **`extend` parameter on every move op** (`moveCursorLeft/Right/
  Vertically/Home/End`, both batch and `*At` forms), set from
  `event->modifiers() & Qt::ShiftModifier` in `keyPressEvent`:
  - `extend == true` (Shift held): step the head via the existing
    movement logic, unconditionally, leaving the anchor untouched.
  - `extend == false` with an active selection: **collapse instead of
    stepping** — jump straight to the near edge (Left/Up/Home →
    `selectionMinAt`) or far edge (Right/Down/End → `selectionMaxAt`),
    matching standard editor convention, then clear the selection
    (anchor = new cursor).
  - `extend == false` with no active selection: unchanged from before
    this phase — step normally, anchor follows to stay collapsed.
  - Vertical movement's single-cursor sticky-column special case
    (ADR 0012) got the same three-way split; a collapsing jump resets
    `m_desiredColumn` to `-1` since the landed column isn't one the
    sticky-column logic was tracking.
- **Escape clears the selection**, not just multi-cursor state:
  `collapseToOneCursor` now also syncs `m_selectionAnchors[0]` when
  already down to one cursor (previously a no-op in that case).
- **Mouse**: plain click sets a fresh cursor+anchor at the point
  (unchanged otherwise); `Shift`+click extends the *last* cursor's
  selection to the click point, anchor untouched — deliberately only
  the last cursor, since combining Shift+click with multi-cursor
  (`Alt`+click, `Ctrl+D`) isn't a scoped interaction for v1. New
  `mouseMoveEvent` override: while the left button is held (checked via
  `event->buttons()`) and exactly one cursor is active, updates that
  cursor's head to the live point under the pointer every move — Qt's
  implicit press-grab already routes move events to this widget
  regardless of `setMouseTracking`, so no extra grab is needed.
  `mouseReleaseEvent` is deliberately **not** overridden: there's
  nothing left to do at release — the selection persists exactly as
  the drag left it, and Qt needs no explicit ungrab from us.
- **Typing/Backspace/Delete over a selection**: each edit primitive
  checks `hasSelectionAt(i)` first. Insert: delete the selection, then
  insert at the collapse point (both recorded in the same undo group —
  no plumbing changes needed, this pass sits entirely on top of
  Phase 10/ADR 0018's `begin_group`/`record_*`/`end_group`). Backspace/
  Delete: the selection-delete *is* the whole operation, no extra byte
  removed beyond it.
- **Undo/redo restores point cursors only**: the undo stack only ever
  snapshotted plain offsets (ADR 0018), so `applyUndoResult` now also
  resets every anchor to match its cursor — an undo/redo always lands
  with no active selection, same as typing over one collapses it.
- **Rendering**: one new pass in `paintEvent`, inside the same
  translate/clip block as the text-drawing loop and running *before*
  it (so glyphs stay crisp on top of the translucent fill, not
  underneath it). Per cursor with an active selection, per visual line
  the selection spans, `xForColumn` gives the same pixel `x` the text
  renderer would use for that column — reusing the exact run-segmented
  measurement from ADR 0013 rather than an approximate `column *
  charWidth`. A line that isn't the selection's last line gets its rect
  widened by half a char to visually hint the selected newline
  continues. New config key `selection = #45403866` (`background`-
  tinted, ~40% alpha), added to `ase_config_create_default` (so
  `ase_config_get_color` always succeeds, same guarantee `background`/
  `text` have) and to the starter-file template.

## Consequences

Verified live end-to-end against the running `ase_gui`, driven with
`xdotool` under the real X session and captured with `import`:
Shift+Right repeatedly extends a selection and a plain Right collapses
it to the far edge (confirmed via cursor position in successive
screenshots); typing a character while a selection is active replaces
exactly that range — checked by save-to-file round trip (`"hello
world"` + select `"o worl"` + type `X` → saved file reads `"hellXd"`)
— and one `Ctrl+Z` restores the original text as a single action
(same-session round trip). Click-drag selection was verified two ways:
cursor tracking the drag endpoint (screenshot), and — because the
overlay's ~40% alpha over a near-black background is visually subtle
in a downscaled screenshot — a pixel-level check confirmed the
expected alpha-blended color (`(52,50,46)`, matching `#45403866` over
`#282828`) filling exactly the selected columns. The low-contrast
overlay is as designed (background-tinted, not a separate hue, per the
project's "one font color" aesthetic pillar — same reasoning ADR 0007
used for syntax highlighting), not a rendering bug; worth revisiting
only if reported as hard to see in practice.

Not handled, deliberately, as v1 scope: Shift+click and drag-selection
both operate on a single cursor only (no defined behavior combined
with `Alt`+click/`Ctrl+D` multi-cursor); overlapping multi-cursor
selections during a batch edit aren't specially guarded (same
implicit-non-overlap assumption every existing multi-cursor op
already carries, per ADR 0012). Clipboard (Phase 12) and find/replace
(Phase 13) build directly on `hasSelectionAt`/`selectionMinAt`/
`selectionMaxAt`/`deleteSelectionAt` added here.
