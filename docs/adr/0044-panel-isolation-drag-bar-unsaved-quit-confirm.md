# ADR 0044: Panel isolation, full drag bar, unsaved-quit confirmation

## Status

Accepted

## Context

Three pieces of direct feedback:

1. With a floating panel (Help, About, Find, Command line, File
   browser) open, the document underneath could still be typed into
   and clicked on — nothing actually made a panel modal.
2. Quitting (window close button, Alt+F4, or `Ctrl+Q`) with unsaved
   changes silently discarded them.
3. Panels could only be dragged by their small `LetterBadge`, not the
   rest of the header bar around it.

A first pass at #1 also closed a panel on a click outside it — direct
follow-up feedback rejected that: a double-click on the header bar, or
a plain click in the middle of the panel's own content, was also
closing it, which felt broken. The request narrowed to "closing is
Escape-only," which turned out to be the right call — see below for
why click-to-close couldn't be made reliable anyway. That same
follow-up also flagged the drag handle's hover cursor and the quit
dialog's native, un-themed look.

## Decision

### Modal isolation for the five `FloatingPanel` popups

`EditorViewport::isModalPanelOpen()` checks `isVisible()` on
`m_findBar`/`m_fileBrowser`/`m_commandLine`/`m_helpPanel`/`m_aboutPanel`
— the complete set of `FloatingPanel`-derived popups (`docs/adr/0022`).
The Output panel is deliberately excluded: it's a plain `QWidget`, not
a `FloatingPanel`, a persistent dock-style console meant to stay open
alongside editing rather than a glance-act-dismiss modal.

`keyPressEvent`, `mousePressEvent`, `mouseMoveEvent`, and `wheelEvent`
all guard on this at the top, and simply do nothing while a panel is
open — no typing reaches the document, no click moves the cursor, no
scroll or hover popup fires. **Closing is Escape-only**, handled
entirely by each panel's own existing `eventFilter` (watching its
focused input widget for `Key_Escape`) — `EditorViewport` never closes
a panel itself.

That "just block, don't also try to close" design isn't a
simplification for its own sake — a click that reaches
`EditorViewport` while a panel is open only got there because some
non-interactive descendant *inside* the panel (a `QLabel`, empty
layout space) ignored the press and Qt bubbled it up, and a genuine
click outside the panel arrives at `EditorViewport` exactly the same
way. The two aren't reliably distinguishable at this level, and a
double-click bubbles the same way too (Qt's default
`mouseDoubleClickEvent()` forwards to `mousePressEvent()`). Chasing
every such bubbling path to tell "inside" from "outside" apart isn't
worth it when the actual request is simpler: just block, and let
Escape do the one job of closing.

### A real bug found along the way: drag events leaking to the document

Extending the drag handle from the badge alone to the whole header bar
(below) surfaced a genuine pre-existing bug in
`FloatingPanel::eventFilter`: it always returned
`QWidget::eventFilter(...)` (`false`) after handling a drag
press/move/release, meaning the event was never actually *consumed* —
Qt's normal "an ignored event propagates to the parent" rule then
walked it from the drag handle up through `content`, the panel itself,
and into `m_host` (`EditorViewport`). This was latent even before this
change: a plain click on the old narrow badge handle (no drag motion)
would have silently moved the document cursor underneath the panel the
same way; it just never surfaced as a visible bug before `EditorViewport`
had any logic that reacted to receiving a press while a panel was open.
Fixed by returning `true` (event fully consumed) from the three drag
branches in `eventFilter`.

### A second bug, found via the same feedback: focus stranded after a drag

Even after the fix above, releasing a drag left Qt's keyboard focus on
`EditorViewport` rather than back on the panel's own input widget —
confirmed with temporary debug instrumentation
(`QApplication::focusWidget()` logged from `EditorViewport::keyPressEvent`,
added, used to diagnose, then fully removed). Practical effect: Escape
— now the *only* way to close a panel — could silently do nothing
right after a drag. Fixed with a new virtual
`FloatingPanel::restoreFocusAfterDrag()` hook (empty by default),
called right after a drag's mouse-release; each of the five panels
overrides it to reclaim focus the same way it already does at open
time (`HelpPanel` → its scroll area, `AboutPanel` → its body label,
`CommandLine`/`FileBrowserPanel` → their line edit, `FindBar` → whichever
of find/replace is actually showing).

### No special drag cursor

`setDragHandle()` used to set `Qt::SizeAllCursor` on the handle —
appropriate for a small badge, but distracting across an entire header
bar. Removed; hovering a panel's header now shows the normal pointer,
matching the rest of this app's chrome (nothing else changes cursor
shape on hover).

### Full header bar as the drag handle (Help, About only)

Checked all five panels' layouts before changing anything:
`HelpPanel`/`AboutPanel` have a real header row (badge + title +
stretch) with room to spare, so both got theirs promoted to a real
`QWidget` (`setDragHandle` needs an actual widget to
`installEventFilter` on — a `QHBoxLayout` added directly via
`addLayout` isn't one), with the badge and title marked
`Qt::WA_TransparentForMouseEvents` so a press anywhere across the bar
— not just directly on the badge — reaches it.

`FindBar`, `CommandLine`, and `FileBrowserPanel` were **not** changed:
their "header" row is just a badge plus a full-width interactive
`QLineEdit` with no spare bar space — extending the drag zone there
would mean either shrinking the input's clickable area or dragging
from on top of it, neither of which is what "drag from the bar" means
for a search/command/filter box. Left as badge-only (they still needed
the `restoreFocusAfterDrag()` fix above, since the underlying drag
mechanism — and its focus-stranding bug — is shared by all five).

### Unsaved-changes confirmation on quit, themed to match

`main.cpp` gained a small `MainWindow : public QMainWindow` (no
`Q_OBJECT`, no signals of its own — just one virtual override, so no
moc pass needed for a class defined right in the file) overriding
`closeEvent()`: if `viewport->isDirty()`, a confirmation dialog asks
"Quit without saving?" (Discard / Cancel, Cancel default);
`event->ignore()` on anything but Discard. New
`EditorViewport::isDirty()` public accessor exposes the existing
private `m_dirty` flag for this.

Built as a manually-constructed `QMessageBox` rather than the static
`QMessageBox::warning()` convenience function, specifically so a
stylesheet could be applied before showing it — the default renders
with the native OS palette and a colored warning icon, jarring against
this app's flat, dark, single-accent chrome everywhere else
(docs/adr/0022's floating-panel system; `applyStatusBarTheme` already
re-themes the native `QStatusBar` the same way, for the same reason).
Colors are pulled from the same `panelBackgroundColor()`/
`panelBorderColor()`/`textColor()` accessors the floating panels
themselves use, so it tracks theme changes/hot-reloads consistently;
`setIcon(QMessageBox::NoIcon)` drops the colored triangle, matching the
"one font color" pillar (docs/adr/0007).

Required reordering `main()` slightly (`EditorViewport` now
constructed before `MainWindow`, since the window needs the viewport
pointer at construction) — no behavior change beyond that reorder.

## Consequences

Verified live end-to-end, each with a real screenshot: typing while
Help panel is open no longer touches the document; a double-click on
the header bar and a plain click in the panel's middle both leave it
open (no more accidental closes); a real drag from empty header-bar
space actually moves Help/About panels and Escape immediately
afterward now reliably closes them (confirmed the fix — this failed
before `restoreFocusAfterDrag()` existed); the quit dialog now renders
in the app's own dark palette instead of the native OS one, with
Cancel/Escape leaving the app running and Discard actually quitting.
