# ADR 0027: Logo, title-color bug, thin scrollbars, faster animation, output divider

## Status

Accepted

## Context

Direct user feedback immediately after ADR 0026's keybinding/Help/
About round, several threads:

1. A logo file (`ase.png`, hand-drawn brush-script wordmark) should
   become "the main image for the editor" and appear in the About
   panel.
2. The Help/About panel titles rendered **black** — visibly wrong
   against the dark theme.
3. Scrollbars in Help and the output panel should be thin, thickening
   slightly on hover.
4. Animations don't cover everything — pressing Enter to start a new
   line doesn't glide — and everything that does animate (including
   the caret blink) should be a little faster.
5. The output panel "feels like part of the main text editing panel" —
   wanted for simplicity, but asked for a small recognition mark at
   the seam: explicitly **not** a full-width line, "not even half
   one."

## Decision

### Logo (`gui/resources/ase.png`, bundled via Qt resources)

Moved from the repo root into `gui/resources/`, added to a new
`gui/resources/resources.qrc`, `CMAKE_AUTORCC ON` in
`gui/CMakeLists.txt` so it's compiled into the binary rather than
loaded from a runtime filesystem path (fragile — depends on the
working directory the app happens to be launched from). Used two
places: `QApplication::setWindowIcon` in `main.cpp` (the actual
"main image for the editor" — title bar/taskbar), and a scaled
`QPixmap` in `AboutPanel`, above the app name.

### Title-color bug

Both `HelpPanel` and `AboutPanel` built their title `QLabel` as a
local constructor variable, styled with a bold font but never given
an explicit palette — so it rendered in Qt's default (effectively
black) text color regardless of this app's theme, the one `QLabel` in
each panel `refreshTheme()` didn't touch. Fixed by promoting both to
member (`m_title`) and setting their palette alongside the body text's
in `refreshTheme()`. Caught by actually looking at a screenshot, not
by confirming the panel opened — same lesson as ADR 0026's about-panel
mojibake bug.

### Thin scrollbars (`gui/src/scrollbar_style.{h,cpp}`)

New shared `thinScrollBarStyleSheet(handleColor, handleHoverColor)` —
6px at rest, 10px on hover, flat, no arrow buttons, applied via
`setStyleSheet()` to `HelpPanel`'s `QScrollArea`, `OutputPanel`'s
`QPlainTextEdit`, and (for consistency — not explicitly asked, but
leaving it native would have looked like an oversight once the other
two matched) `FileBrowserPanel`'s `QListWidget`. This is the one
deliberate stylesheet in a codebase that otherwise paints everything
itself via `QPainter` — `QPalette` has no equivalent for scrollbar
width or hover state, so there's no non-stylesheet way to get this;
scoped strictly to `QScrollBar` selectors, not a general styling
escape hatch. Colors are pushed in by the caller (from
`EditorViewport`'s theme accessors, low alpha at rest, higher on
hover) — the helper itself has no config/theme access, same shape as
every other bit of shared chrome.

### Animation coverage and speed

- **Enter now glides**: the `Qt::Key_Return`/`Key_Enter` case in
  `EditorViewport::keyPressEvent` no longer sets `isEdit = true`, so
  `snapAnimationToTarget()` is skipped and the caret animates to the
  new line like any navigation action, when `animations` is on.
  Regular character insertion is untouched and stays instant — ADR
  0017's original reasoning (gliding can't keep pace with fast
  repeated small jumps, and reads as lag rather than smoothness) still
  holds for that case; a newline is one discrete jump, not a rapid
  sequence, so it doesn't have the same problem.
- **Faster across the board**, all "a little," not dramatically:
  `kEaseFactor` (caret glide/smooth-scroll convergence per frame) 0.35
  → 0.5; `kCaretAnimationTicks` (smooth-fade period) 34 → 24 (~1020ms
  → ~720ms); the hard blink's idle-toggle interval 17 → 12 ticks
  (~500ms → ~360ms); `FloatingPanel`'s open/close pop 150ms → 110ms;
  `FileBrowserPanel`'s sliding row highlight 110ms → 85ms.

### Output panel seam marker

A short (40×2px), centered `TranslucentBar` at the very top of
`OutputPanel`'s own layout — not a full-width rule, not even half one,
per the user's explicit words. Filled with the plain text color (no
new hue). Lives *inside* `OutputPanel` rather than as a separate row
in `main.cpp`'s layout specifically so it shows and hides together
with the panel with zero extra wiring — no visibility signal needed
between `EditorViewport::toggleOutputPanel()` and a would-be sibling
widget.

`TranslucentBar` itself moved out of `file_browser_panel.{h,cpp}`
(where it was written for the row-highlight bar, ADR 0024) into its
own `gui/src/translucent_bar.{h,cpp}` now that a second, unrelated
panel needs it — a real, current duplication once `OutputPanel` needed
the same "paint a flat rect via `QPainter`, not `QPalette`" shape, not
a hypothetical-future extraction.

One more bug found while verifying the divider, not originally
reported: `OutputPanel` itself never had `setAutoFillBackground(true)`
or a themed palette, so every pixel not covered by a child widget —
which includes all the empty space around the divider bar — rendered
Qt's default light-gray widget background, a jarring gray band right
across the seam. Fixed by giving `OutputPanel` itself an explicit
`QPalette::Window` matching `EditorViewport::backgroundColor()`,
alongside the existing child-widget theming.

## Consequences

Verified live via `xdotool`: the About panel shows the logo — which,
by coincidence, already used almost exactly this app's own text color,
so it reads as if it were designed for the theme; Help/About titles
now render in the theme's text color, not black; the output panel's
divider renders as a small, correctly-dark-backed tick mark (confirmed
both before and after the background-bug fix, so the fix is provable,
not assumed); a 40-line compile output showed a visibly thin
scrollbar; Enter/typing/save round-tripped correctly through a save-
to-file check, confirming the animation-path change didn't disturb
correctness. Full test suite unaffected (GUI-only) — 9/9 still
passing.

Not built: hover-state motion itself isn't captured in a screenshot
(same tooling limit ADR 0022/0024 already noted) — verified by code
review of the QSS rather than a frame capture. Vim mode remains the
next planned phase.
