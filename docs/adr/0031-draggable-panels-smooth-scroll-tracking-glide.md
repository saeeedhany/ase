# ADR 0031: Draggable panels, smooth scroll, tracking-popup glide, Tab

## Status

Accepted

## Context

Direct user feedback, framed explicitly as "finish the base editor before
side quests" rather than a new feature request:

1. Floating panels (Find/Replace, Help, File browser, Command line,
   About) should be draggable.
2. Scrolling inside a panel (named example: the Help panel) should be
   as smooth as the main editor's own scroll glide — and this needs to
   be architected so **future** panels inherit it automatically, not
   bolted on per-panel each time something's noticed.
3. The LSP hover tooltip should fluidly follow the pointer while still
   hovering the same symbol, not sit frozen at wherever it first
   appeared.
4. The completion popup should track the caret smoothly as more of the
   word gets typed, not jump.
5. "You missed the find/replace bar" for animation, and: whatever gets
   built for #3/#4 should carry "the editor's identity," consistently,
   automatically — again framed as an architecture ask, not "fix this
   one panel."
6. Tab does nothing (a real, reported gap — not a deliberate omission).

Point 5's specific FindBar claim was checked directly rather than
assumed: a temporary debug print in `FloatingPanel::openPanel()`
confirmed `m_animated=1` and a full pop-in/pop-out/resize animation
firing identically to every other panel, including on every Find→
Replace mode toggle (each `openFor()` call re-invokes `openPanel()`,
which replays the pop against the new size). No code was changed for
this specific point — logged here so the investigation isn't lost.

## Decision

### Draggable panels: one mechanism in `FloatingPanel`, one call per panel

`FloatingPanel::setDragHandle(QWidget *handle)` installs drag-by-mouse
handling (press/move/release via an event filter, clamped to stay
inside the host) once, in the base class. Every panel already has a
`LetterBadge` (F/R, ?, O, i, :) as its compact identity marker — that
badge, not a new dedicated title-bar widget, is the drag handle
(`setDragHandle(m_badge)`, one line in each of the five panels'
constructors). No layout restructuring needed anywhere: every header
row was already a bare `QHBoxLayout`, not a wrapping `QWidget`, and
stays that way.

A drag is deliberately **not** persisted: `recenter()` (host resize)
and every `openPanel()` (which always calls `revealForSetup()`, which
always repositions to `targetGeometry()`) override it. A future panel
gets dragging by making the exact same one-line call — nothing else to
implement, and nothing to remember to keep in sync if this behavior
ever changes.

### Smooth scroll: `installSmoothScroll()`, works on any `QAbstractScrollArea`

`QScrollArea` (Help), `QListWidget` (File browser), and
`QPlainTextEdit` (Output) all derive from `QAbstractScrollArea` — a
single free function, `installSmoothScroll(QAbstractScrollArea *area,
EditorViewport *viewport)` (`gui/src/smooth_scroll.{h,cpp}`), installs
an event filter on `area->viewport()` (where Qt actually delivers
wheel events — the same widget `QAbstractScrollAreaPrivate` filters
internally) that eases the vertical scrollbar's `value` toward each
notch's target via `QPropertyAnimation`, instead of Qt's native instant
jump, and consumes the event outright so that jump never runs. Reads
`viewport->animationsEnabled()` fresh on every wheel event rather than
needing a pushed-in flag kept in sync on config reload — nothing to
update, ever, after the one `installSmoothScroll(...)` call site.
Applied to all three widgets above; a future scrollable panel gets it
the same way.

### `TrackingPopup`: the shared base CompletionPopup and HoverPanel needed

Both the completion popup (Phase 17, ADR 0030) and the hover tooltip
had nearly identical, independently-duplicated fade/paint code, and
neither could satisfy "follow the pointer/caret fluidly" without
becoming its own small animation system. Extracted into
`gui/src/tracking_popup.{h,cpp}`, a base neither derives from
`FloatingPanel` (still true for the reasons ADR 0030 gave — these
refresh far more often than a glance-act-dismiss chrome window and
shouldn't replay a scale-pop on every keystroke/mouse move) but which
now **is** the shared "identity" for this category of overlay:

- the same flat `panel_background`/border paint as every other piece
  of chrome (`TrackingPopup::paintEvent`; a subclass calls it first,
  then draws its own content in a second `QPainter` in the same
  `paintEvent` — legal and idiomatic, two sequential scoped painters
  on the same widget)
- a fade, only on the hidden→visible edge (unchanged from ADR 0030)
- **new**: `retarget(pos, size)` — resizes instantly, but *moves* by
  gliding (`QPropertyAnimation` on Qt's own `pos` widget property,
  110ms `OutCubic`) toward the new anchor whenever already visible,
  snapping only on first appearance (sliding in from a stale point on
  arrival would look wrong; the fade already carries that beat) or
  with `animations` off.

`CompletionPopup` and `HoverPanel` now derive from `TrackingPopup` and
keep only what's actually theirs: item-list storage/rendering for one,
a `QLabel` + `QFontMetrics`-measured sizing for the other. A future
tracking overlay (say, a diagnostics-on-hover tooltip) derives from
`TrackingPopup`, implements its own content and a `retarget()` call,
and gets fade/paint/glide for free — the identity this ADR's context
section asked for.

`HoverPanel` gained `moveTo(pos)`: `EditorViewport::scheduleHoverRequest`
now calls it (a lightweight, no-server-round-trip reposition) whenever
the pointer is still within the word range the open tooltip already
covers, instead of doing nothing — previously the tooltip stayed
frozen at wherever it first appeared even as the pointer kept moving
within the same word. `CompletionPopup` needed no equivalent change:
`showItems()` was already called fresh on every request response with
the caret's current position, so switching its positioning from an
instant `setGeometry` jump to `retarget`'s glide was enough on its own
to make it visibly track the caret while typing.

### Tab now indents

`Qt::Key_Tab`'s `event->text()` is `"\t"`, a control character —
`QChar::isPrint()` is false for it, so it fell all the way through to
`QWidget::keyPressEvent` (Qt's default focus-traversal handling),
meaning it did nothing: a real gap, not a deliberate v1 omission.
Inserts **four spaces**, not a raw tab byte: `drawLine`/`xForColumn`
measure each run with plain `QFontMetrics::horizontalAdvance` (no
`QTextLayout`, no tab-stop expansion), so a literal `'\t'` would
measure at ~0 width and render as an invisible non-indent. A soft tab
renders correctly with the exact same per-glyph measurement every
other character already uses — simpler than adding tab-stop-aware
rendering for one key, and consistent with this codebase's byte-level
column model (ADR 0012). Treated as a plain character insertion
(instant, not glided), same as any other typed character.
Completion-popup Tab-to-accept (ADR 0030) is intercepted earlier in
`keyPressEvent` and never reaches this new case.

## Consequences

All 9 ctest suites still pass (this phase is GUI-only, no core
changes). Verified live via `xdotool` (absolute screen coordinates —
`xdotool click`/`mousedown`/`mouseup` don't actually support a
`--window` flag the way `mousemove` does; an early drag/scroll test
that appeared to do nothing was this, not a code bug, confirmed by
retrying the identical action with computed absolute coordinates):
dragging the Help panel by its badge moved it and reopening reset it
to center; wheel-scrolling inside the Help panel now eases through
content instead of jumping (confirmed both that the event reached the
new filter and that the visible content actually advanced); typing
`pri` → `printf` showed the completion popup's position visibly
sliding right to track the caret at each new character, accepting
correctly replaced the prefix with no reopen-loop; hovering `value`
then moving a few pixels right while still over the same identifier
showed the tooltip's position shift to follow, without a flicker or a
fresh server request; Tab at a fresh line start inserted exactly four
spaces.

Not attempted: persisting a dragged panel position across host resizes
or across close/reopen (a deliberate simplification — see the drag
section above); a configurable indent width/tabs-vs-spaces choice for
the new Tab behavior (four spaces is hardcoded, matching how other
layout constants like the gutter padding are hardcoded rather than
config keys); smooth *size* transitions for `TrackingPopup` subclasses
(only position glides — a resize-while-visible still snaps instantly).
