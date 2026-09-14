# ADR 0073: The command line moves to the status bar

## Status

Accepted

## Context

The `:` command line was a centred floating panel, one per buffer, in
the same family as Find/Replace and Open/Save-As ([ADR 0022](0022-floating-panel-design-system.md),
[ADR 0025](0025-command-line-and-compile.md)).

Every other part of vim's bottom line had already arrived in the status
bar and stayed there: the mode label ([ADR 0046](0046-vim-mode-phase-1.md)),
messages ([ADR 0062](0062-the-status-bar-message-line.md)), the
language-server segment ([ADR 0063](0063-language-server-state-in-the-status-bar.md)),
and the position readout ([ADR 0023](0023-editor-chrome.md)). Command
entry was the one piece that floated somewhere else.

That mattered more with `/` search coming. A floating `/` is not what
anyone who types `/` is expecting, and putting it in a panel would have
meant two search surfaces with no clear division between them.

There is precedent for moving chrome off centre rather than defending
the design system: [ADR 0026](0026-keybinding-scheme-help-about-find-bar-position.md)
moved find/replace to the top-right on direct feedback that a centred
panel sat on top of the text being searched. A `:` prompt over the code
is the same complaint one step further.

## Decision

### The `:` line lives in the status bar

`CommandLine` stops being a `FloatingPanel` and becomes a plain widget
in the status bar: a prompt label and a `SmoothLineEdit`, no badge, no
frame, no scale+fade. The prompt character is the affordance, which is
what vim has always relied on.

One instance for the window rather than one per buffer, re-pointed at
whichever viewport is active — the shape `OutputPanel` already uses,
and for the same reason: there is one status bar.

### While the prompt is up, it takes the mode and message area

The bar has four tenants. The prompt displaces the two on the left and
leaves the two on the right alone:

| | closed | prompt open |
|---|---|---|
| left | mode label | prompt + field |
| middle | message | prompt + field |
| right | LSP segment | unchanged |
| far right | Ln, Col | unchanged |

Displacing the position readout was never considered — `:42` is a
command about a line number, and hiding the line number while it is
typed would be perverse. Vim keeps the ruler for the same reason.

### It fades; it does not scale

Floating panels open with scale+fade ([ADR 0022](0022-floating-panel-design-system.md)).
This does not. The status bar is a fixed strip, and scaling a line of it
reads as the chrome itself moving rather than as something appearing in
it. A fade at the existing `kChrome` tier ([ADR 0053](0053-one-motion-language.md)),
no new duration.

### Find/Replace stays a floating panel

`Ctrl+F` is unchanged. It has two fields, which do not fit on one line,
and vim's own answer to replace is `:s/a/b/` — through the command line
anyway. So the division is by shape, not by audience:

- **status bar line** — `:` commands, and `/` `?` search: one field
- **floating FindBar** — find *and* replace: two fields

`Ctrl+;` still opens the command line, now the status-bar one. Same
two-bindings-two-audiences split as `gd`/`F12` ([ADR 0067](0067-go-to-definition.md)).

## Consequences

`CommandLine` is no longer a `FloatingPanel`, so it loses that base's
animation, dragging, snapshot machinery and theming hooks — all of which
it either did not want or now does differently. `isModalPanelOpen()`
asks it `isPromptOpen()` rather than `isVisible()`, since a status-bar
widget's visibility is managed by the bar.

One trap, worth writing down because it cost real time: **`QStatusBar`
sizes itself from every item it holds, including hidden ones.** A
`QLineEdit`'s size hint is two pixels taller than a `QLabel`'s, so
merely adding this widget grew the bar by 2px and moved every other
label in it — with the prompt closed, and on every screen. The line
edit's height is now pinned to the prompt label's. `QStatusBar::addWidget()`
also shows what it is given, undoing a `hide()` in the constructor.

A ~2px gap remains between the prompt character and the text, from
`QLineEdit`'s own fixed internal margin, which `setTextMargins(0,…)`
does not remove. Left alone rather than subclassing the paint path for
two pixels.

`/` and `?` are now a prefix on an existing surface rather than new UI,
which is the point of doing this first.
