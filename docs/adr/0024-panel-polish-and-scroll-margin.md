# ADR 0024: Panel animation rework, file-browser search/highlight, scroll margin, status bar theme

## Status

Accepted

## Context

Direct user feedback after Phase 14 landed, three items:

1. The `FileBrowserPanel` had rough edges: the close animation felt
   unsmooth, up/down navigation felt unsmooth, and the path field
   showing the full absolute path was the wrong interaction — "I'll
   search when I want my specific file," with a fluent, visually clear
   indicator of the targeted file.
2. The cursor should keep some lines/characters of context visible
   before it reaches the exact edge of the viewport, both vertically
   and horizontally (a "scrolloff" behavior).
3. The status bar wasn't styled to match the app's theme.

## Decision

### 1a. Floating-panel animation: animate a snapshot, not real geometry

Root cause of "the close animation feels not smooth": `FloatingPanel`
was animating its own `geometry` directly — a real `QWidget` with a
live `QVBoxLayout` (and, for `FileBrowserPanel`, a `QListWidget`
inside it). Animating geometry meant every frame forced Qt to actually
relayout that whole child tree, not just redraw it — expensive, and
visibly janky, worse the more content a panel had (why `FindBar`'s
simpler content made this less obvious there).

Fixed by animating a **static snapshot image** instead: `FloatingPanel`
now owns a `QLabel` (`m_snapshot`, `setScaledContents(true)`) and a
plain `QWidget` (`m_content`) that subclasses build their real UI on
(via the new `contentWidget()`, replacing direct children of `this`).
Opening/closing: grab a `QPixmap` of `m_content` once via `grab()`,
hide the real content, animate `m_snapshot`'s `geometry` (a plain
image-filled label with no children — resizing it is just a scaled
blit, not a relayout) and opacity together via
`QParallelAnimationGroup`, then swap back to the real, interactive
content once the animation finishes. `this` panel's own paintEvent
(flat background + border) is skipped while `m_showingSnapshot` is
true, since that appearance is already baked into the grabbed pixmap.

This surfaced a real, separate class of bug during implementation,
worth recording since it cost the most debugging time: **a widget's
geometry set while hidden doesn't reliably survive that widget's first
`show()`**. Concretely:
- `QWidget::setGeometry()` schedules child-layout recomputation for the
  next event-loop pass, not immediately — fixed by forcing it
  synchronously (`layout()->activate()`) in a new
  `syncContentGeometry()`, called from `resizeEvent`.
- Even with that, a **freshly-shown** child (here, `QListWidget`,
  specifically once it becomes visible for the first time) can trigger
  Qt's own first-show auto-sizing that overrides an already-correct
  geometry *after* the fact — geometry read back correctly right up
  until the `show()` call, then didn't. Fixed by re-asserting geometry
  again immediately after showing, not just before — see
  `FloatingPanel::revealForSetup()`.
- `FileBrowserPanel::setDirectory()` (which populates the list and
  computes the initial highlight-bar rect) has to run *between*
  `revealForSetup()` and the snapshot grab — not before
  `revealForSetup()` (the list would be measuring itself against the
  panel's tiny pre-layout default size) and not after the grab (the
  snapshot would show an empty list). `openFor()` now calls
  `revealForSetup()` explicitly before `setDirectory()`; `openPanel()`
  calls it again internally before grabbing, which is safe — every
  step in it is idempotent once geometry has actually settled.

### 1b. File browser: filter field, not a path field

Per the user's explicit direction: the field no longer shows or
accepts a literal path. `setDirectory()` sets it as a placeholder
(`QLineEdit::setPlaceholderText`, e.g. `"browse2"`, not
`".../scratchpad/browse2/"`) and clears any typed text. Typing now
filters the listing (`applyFilter`, case-insensitive substring on each
row's own text via `QListWidgetItem::setHidden`, `..` always exempt)
and auto-selects the first match, so **Enter picks whatever's
currently, visibly highlighted** — the "search fluently, select
visually" flow asked for. `Up`/`Down` are forwarded from the filter
field to the list (skipping hidden/filtered rows via a new
`nextVisibleRow`), so typing-to-filter and arrowing-to-refine compose
the way a quick-open field normally does.

Confirm semantics split by mode, since Open and Save-As have
genuinely different needs here: **Open** confirms whichever row is
currently highlighted (there's no reason to open a file that isn't in
the listing). **Save-As** instead reads the filter field's own text as
the filename to save — typing a brand new name that isn't in the
listing yet is the entire point of Save-As, so its confirm path
deliberately doesn't depend on list/filter matching at all.

### 1c. A genuinely smooth, visible selection highlight

Replaced reliance on the list's native (OS-styled, instant, no
animation) selection rect with a custom **sliding highlight bar**: a
small widget (`m_rowHighlight`, child of the list's viewport)
positioned via `QPropertyAnimation` on `geometry` (110ms, `OutCubic`)
to whichever row is current, driven by `currentRowChanged`. The native
highlight is made invisible (`QPalette::Highlight` == `Base`) so the
two never visually compete. Reuses `EditorViewport::selectionColor()`
(a new accessor) — the exact same translucent tone the editor's own
text selection already uses, one consistent "this is highlighted"
color instead of a new one invented for this list.

This is also where the ".." *hiding* bug was found and fixed (not
originally reported, found while verifying the highlight fix): the bar
was styled via `QPalette::Window` + `setAutoFillBackground(true)`,
which **silently ignores a `QColor`'s alpha channel** and paints fully
opaque — at a tone close enough to the surrounding dark palette that
it completely hid whatever row text sat underneath, rather than
tinting it, and did so invisibly enough to read as "the row is just
missing." Fixed with a small dedicated `TranslucentBar` widget
(`gui/src/file_browser_panel.h`) that paints via
`QPainter::fillRect(rect(), color)` directly — the same technique
`EditorViewport` already uses for its own selection/find-match
overlays, which never had this problem because it was never routed
through `QPalette`/auto-fill in the first place.

### 2. Scroll margin ("scrolloff")

`EditorViewport::ensureCursorVisible()` previously scrolled only once
the cursor reached the *exact* edge of the viewport. Now keeps
`kVerticalScrollMargin` (3 lines) / `kHorizontalScrollMarginChars` (4
characters) of context visible around the cursor before scrolling
kicks in, in both directions, both axes — symmetric with the existing
vertical/horizontal split (ADR 0014, decision 3). Both margins are
clamped to at most half the viewport (`(visibleLines-1)/2` /
`(textAreaWidth-kCaretWidth)/2`) so a short or narrow window degrades
to less context instead of oscillating or refusing to scroll.
Verified live: pushing the cursor down through a 60-line file, the
view consistently stops scrolling with exactly 3 lines still visible
below the cursor, never fewer.

### 3. Status bar theme

`QStatusBar` defaults to a native, light OS-styled bar — the same
"native chrome clashing with the app's dark palette" problem the
floating panels' `QLineEdit`s had (ADR 0022). Themed via `QPalette`
(`Window`/`WindowText` from `EditorViewport::backgroundColor()`/
`textColor()`) plus `setAutoFillBackground(true)` — here that's
correct, since the status bar's colors are always fully opaque (no
alpha-ignoring bug like 1c above, which only bit a *translucent*
color). Re-applied on every `statusChanged` emit (cheap — a handful of
`QPalette::setColor` calls), so a hot-reloaded config color reaches
the status bar too, not just the editor. Also switched from
`addWidget` to `addPermanentWidget` (no "temporary message" separator
styling) and disabled the size grip, both in service of the same flat,
minimal look.

## Consequences

Verified live via `xdotool`: the file browser's `..` entry and the
sliding highlight both render correctly (confirmed the earlier
"missing" state and the fix, not just the final state); typing a
filter (`"hon"` + Enter) opens the correctly-matched file; arrow-key
navigation moves the highlight to the right row; Save-As with a typed
new filename still saves correctly; find/replace (Phase 13) still
round-trips correctly through the rebuilt `FloatingPanel` base,
confirming the animation rework didn't regress `FindBar`. Full test
suite unaffected (GUI-only change) — 8/8 still passing.

Not built: a literal frame-by-frame capture of the animation's motion
(this project's screenshot tooling still can't reliably catch a
~110–150ms partial frame, per ADR 0022's own note) — the smoothness
claim rests on the architectural fix (no more forced relayout per
frame) plus the geometry-correctness bugs above being genuinely fixed,
not on visual motion capture.
