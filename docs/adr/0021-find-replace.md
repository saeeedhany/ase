# ADR 0021: Find & replace — a keyboard-only bar, matches as selections

## Status

Accepted

## Context

Phase 13 of the "complete normal editor" plan. Two design questions
were put to the user before implementing (rather than guessed):
case-sensitivity default, and whether the bar should have clickable
buttons. Both came back in favor of the smaller-footprint option:
**case-insensitive by default, no buttons — keyboard only.**

`QMainWindow::setCentralWidget` only takes one widget, so a docked find
bar needs a structural change to `main.cpp` first.

## Decision

### Structural: a wrapper widget, not a second top-level window

`main.cpp` now builds a plain `QWidget` with a `QVBoxLayout` holding
`FindBar` then `EditorViewport` (stretch factor 1), and that wrapper —
not the viewport directly — becomes the central widget. `FindBar`
starts hidden and takes no layout space until opened.

### `FindBar` (`gui/src/find_bar.{h,cpp}`): input widgets only, no logic

One `QLineEdit` (find) always present when open; a second (replace),
wrapped in its own row widget so it can be shown/hidden as a unit,
appears only in Replace mode (`Ctrl+H` vs `Ctrl+F`). No `Q_OBJECT`, no
custom signals — `EditorViewport` doesn't declare any either (nothing
in this codebase does yet), so `FindBar` reaches it through a plain
pointer (`EditorViewport::setFindBar`, called once from `main.cpp`
after both are constructed) and calls its public find/replace methods
directly. Key handling (`Return`/`Shift+Return`/`Ctrl+Return`/`Escape`)
is done via `installEventFilter` on both line edits rather than
subclassing `QLineEdit` — smaller diff, and the two fields need
identical Escape handling regardless of which one has focus.

No buttons, per the user's choice above — every action is a keybinding:
`Enter` in the find field → next match (wraps); `Shift+Enter` →
previous; `Enter` in the replace field → replace the current match and
advance; `Ctrl+Enter` → replace all. `Escape` from either field hides
the bar and returns focus to the viewport. Opening the bar pre-fills
the find field from the viewport's current single-line selection (not
multi-line — a multi-line starting needle is more surprising than
helpful), matching common editor convention, reusing
`EditorViewport::primarySelectionText()`.

### `EditorViewport`: matches are selections in disguise

- **Search**: plain substring, no regex (matches this project's
  standing "minimal" pillar — no coalesced undo, no whole-line copy
  fallback, same call here). Case-insensitivity is `QByteArray::
  toLower()` on fresh copies of both needle and haystack per
  recompute — ASCII-only, a documented v1 simplification consistent
  with this codebase's existing byte-level-cursor/no-IME shortcuts
  (ADR 0006). Because ASCII case-folding never changes a byte's
  length, the matched range's length always equals the needle's own
  byte length — no separate bookkeeping needed for that.
- **`m_matches`/`m_currentMatch`** hold the flat list and current
  index; `recomputeMatches()` runs from the existing `refreshCache()`
  choke point unconditionally (a no-op when there's no active query),
  so matches never go stale after *any* edit — typing elsewhere in the
  document, undo/redo, or a find/replace action itself — without a
  second hook.
- **`jumpToMatch(index)` is the one place that actually moves the
  cursor**: it sets `m_cursors`/`m_selectionAnchors` to the match's
  `[start, end)` range — i.e. **the current match just becomes an
  active selection**, reusing Phase 11's model exactly. This is the
  key reuse of the phase: `replaceCurrentMatch` doesn't need its own
  delete/insert logic at all, it's just `insertText(replacement)` over
  whatever selection `jumpToMatch` already set up — the same path any
  other typed-over-a-selection edit takes, undo group included.
- **`replaceAllMatches`** is the one function that *doesn't* go through
  the selection path (there's no single "current selection" for N
  simultaneous matches) — it writes directly through
  `ase_buffer_delete`/`_insert`, highest-offset-first, as one undo
  group — the same discipline every other multi-offset batch edit in
  this file already follows (ADR 0012's multi-cursor loops, ADR 0018's
  undo groups). Leaves the cursor at buffer offset 0 afterward — a
  documented v1 simplification rather than tracking where the "same"
  text landed post-replacement.
- **Rendering**: the selection-highlight code from ADR 0019 was
  factored into a shared `highlightRange(painter, start, end,
  firstLine, lastLine, color)` helper, now called for both the
  selection pass and a new match pass. Regular matches reuse the
  existing `selection` color; the current match gets a new, stronger
  `find_match` config color (`#45403899`, the same RGB as `selection`
  at higher alpha — keeping every overlay in this codebase the same
  tint, only alpha varies, the same reasoning ADR 0019 used) so it's
  easy to spot among several highlighted matches.

## Consequences

Verified live against the running `ase_gui`, driven with `xdotool`:
- `Ctrl+F`, typed a needle present three times — pixel-level inspection
  (not just eyeballing a screenshot, since ADR 0019 already established
  these overlays are subtle at ~40-60% alpha) confirmed all three
  occurrences tinted, with the current match measurably darker/stronger
  than the other two, matching the expected alpha-blended values for
  `selection` vs. `find_match` exactly.
- `Ctrl+H`, replace-one (`Enter` in the replace field) and replace-all
  (`Ctrl+Enter`) each verified via a save-to-file round trip against
  `"foo bar foo baz foo"`, and each undoes as a single `Ctrl+Z` — full
  round trip confirmed for both.
- One real finding during testing, not a bug: after a replace action,
  focus deliberately stays in the replace field (so repeated `Enter`
  steps through matches without re-focusing) — and since this codebase
  has no window-level shortcut system yet (`Ctrl+S` etc. are handled
  in `EditorViewport::keyPressEvent`, which only fires when the
  viewport itself has focus), other editor shortcuts don't reach the
  viewport while a find-bar field is focused. This is consistent with
  every other text field in the app having no special-case handling,
  not a regression — but it's the reason `Escape` (or a click back into
  the viewport) is needed before e.g. `Ctrl+S` while the bar is open.
  Worth revisiting once Phase 14 adds real chrome/shortcut
  infrastructure.

Not built, deliberately: no case-sensitivity toggle, no regex, no
buttons — all per the user's explicit choices for this phase. Phase 14
(editor chrome: status bar, dirty tracking, Open/Save As) is next,
unrelated to find/replace.
