# ADR 0026: Keybinding scheme fix, Help/About panels, find bar repositioned

## Status

Accepted

## Context

Direct user feedback right after Phase 15 landed, several threads at
once:

1. **A real bug**: the bare `:` keypress opened the command line
   unconditionally — meaning `:` could never be typed as a literal
   character into the buffer. `:` as the command-line trigger is a
   Vim convention (the ex-command line); this editor isn't in Vim mode
   yet, so a bare `:` has to stay a normal, typeable character.
2. Wants a **non-conflicting modifier scheme** for the command line,
   compile, and output toggle, since bare `:` was wrong anyway.
3. Wants to **discuss the keybinding scheme** as its own topic, and
   wants a **Help panel** (all keybindings) and an **About panel**
   (app info, author, links) — both centered like the rest.
4. **Find/replace specifically** feels wrong centered — wants it moved
   to the top-right instead, leaving every other panel centered.
5. Raised a question about closing panels / quitting — resolved by
   explicitly choosing to change nothing there (see Decision).

## Decision

### The `:` bug and the new modifier scheme

Removed the bare-`:` interception entirely (`event->text() == ":"`) —
`:` now always inserts a literal colon, like any other character.
Replaced with **`Ctrl+;`** for the command line (matches the visual
association: `;` sits right next to where `:` would be, Shift'd). Two
new direct shortcuts, bypassing the command line for the two `:`
sub-commands used often enough to deserve one: **`Ctrl+B`** compiles
directly (`build_command`, ADR 0025) — not `Ctrl+C`, which is already
Copy; **`Ctrl+Shift+O`** toggles the output panel directly, alongside
the existing (unshifted) `Ctrl+O` for Open — same shifted-variant-for-
secondary-action pattern `Ctrl+S`/`Ctrl+Shift+S` already established.
Both also share their implementation with the `:compile`/`:output`
command-line paths (`EditorViewport::compile()`, new
`toggleOutputPanel()`) rather than duplicating logic.

### Close/quit: deliberately unchanged

Put directly to the user: keep Escape closing whichever panel is
open (already true, per-panel, since Phase 13) and `Ctrl+Q`/`:q`
always quitting the app outright, with no new dual-purpose binding.
Chosen specifically to keep "quit" a single, deliberate, unambiguous
action — never a side effect of a "close panel" gesture that could
misfire from muscle memory. No code changed here; recorded because the
alternative (a Vim-`:q`-style "close panel, or quit if nothing's open"
cascade) was seriously considered and explicitly declined.

### `HelpPanel` (`gui/src/help_panel.{h,cpp}`) — `Ctrl+/`, `"?"` badge

Same `FloatingPanel` family as everything else, but the first one with
no input field: a `QScrollArea` wrapping a rich-text `QLabel`, built
from a hand-maintained two-column HTML table grouped by category
(Navigation, Editing, Find & replace, Files, Build, Command line,
Other). Hand-maintained rather than generated from the keybinding
dispatch code — a generated single source of truth is real, unscoped
work, and the two can drift; accepted as a v1 tradeoff. No color in
the markup itself (only structure) so it inherits the `QLabel`'s own
palette — same "one font color" pillar every other panel already
follows. Escape closes it (the `QScrollArea` is given focus and
installed as its own event filter target, the same pattern every other
panel's input widget already uses, just with a scroll area standing in
for a `QLineEdit`).

### `AboutPanel` (`gui/src/about_panel.{h,cpp}`) — `Ctrl+I`, `"i"` badge

Same shape as `HelpPanel` minus the scroll area (content is short): a
`QLabel` with `setOpenExternalLinks(true)` so the GitHub/website links
are real clickable links, opened via the system browser. Content: app
name, version (`0.0.0` — pulled from `CMakeLists.txt`'s `project()`
call, not invented), one line on what the editor is, "Developed by
Saeed" with the two links the user gave directly
(`https://github.com/saeeedhany`, `https://saeedz.vercel.app` — not
guessed), and a short current-state note (all "complete normal editor"
phases done, Vim mode next, not yet published while the license
decision stays open — pulled from `README.md`'s own wording, not
newly invented).

One real bug caught and fixed during verification: the notes
paragraph used a literal UTF-8 em dash character in the C string
literal, but the code loaded it via `QString::fromLatin1`, which
doesn't understand UTF-8 — corrupted into visible mojibake on screen.
Switched both `HelpPanel` and `AboutPanel` to `QString::fromUtf8` (the
correct match for this file's actual encoding, and the codebase's
convention everywhere else — e.g. the em dashes already throughout
`gui/src/*.cpp` comments).

### Find bar repositioned — the one panel that isn't centered

New `FloatingPanel::Anchor` (`Center` / `TopRight`), set once per
panel (not re-checked per open — a panel's screen position isn't
config-driven the way its colors/animation are).
`targetGeometry()` branches on it. `FindBar` is the only panel that
calls `setAnchor(Anchor::TopRight)`; every other panel keeps the
default. Reasoning, from the user directly: find/replace is a panel
you keep open while actively scanning matches in the text below it —
centered, it sits on top of the very thing you're looking at, in a way
a glance-act-dismiss panel like Open/Save-As doesn't suffer from.

## Consequences

Verified live via `xdotool`: a literal `:` types correctly into the
buffer (confirmed the bug, then the fix); `Ctrl+;` opens the command
line; the find bar renders top-right, clear of the text; `Ctrl+/` and
`Ctrl+I` open Help/About respectively, both correctly themed and
scrollable/link-clickable; the em-dash mojibake was caught by actually
looking at the rendered screenshot, not just confirming the panel
opened; Escape closes both new panels and returns focus to the editor,
confirmed by typing immediately afterward. Full test suite unaffected
(GUI-only change) — 9/9 still passing.

Not built: a generated (rather than hand-maintained) keybinding
reference; any settings/rebinding UI (this app's keybindings are still
compile-time constants, same as every phase before this one). Vim mode
remains the next planned phase, unscoped until its own dedicated
design pass.
