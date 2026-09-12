# ADR 0050: Runtime font-size zoom, and Vim mode on by default

## Status

Accepted

## Context

Two more direct requests, the last before committing the whole recent
batch (ADR 0046–0049): make font size adjustable live, at runtime, via
a keyboard shortcut, rather than only through editing `config.ase` and
waiting for the hot-reload; and make Vim mode the user's own default
going forward, not something opted into per config.

## Decision

### Runtime font-size zoom: `Ctrl+=` / `Ctrl+-` / `Ctrl+0`

Matches the convention every other app already uses (browsers, VS
Code, ...) rather than inventing a new one. `Ctrl+=`/`Ctrl+Plus` zoom
in, `Ctrl+-` zooms out, `Ctrl+0` resets — all three added to the
existing Ctrl-chain in `keyPressEvent`, unconditional on Vim mode (like
Ctrl+S/Ctrl+Q/every other Ctrl shortcut) since `handleVimNormalOrVisualKey`
already bails out immediately for any Ctrl-modified key.

This is a **live, in-session override**, not a write-back to
`config.ase` — the two stay independent:

- `m_fontSizeOverride` (0 = none) tracks the live-adjusted point size.
  `rebuildFont(int pointSize)` — factored out of `applyConfig()`, which
  used to build `m_font`/`m_metrics`/`m_boldMetrics`/`m_lineHeight`/
  `m_charWidth` inline — rebuilds all of that from the remembered
  `m_fontFamily` at a given size, without touching config at all.
  `adjustFontSize(int delta)` clamps to `[kMinFontSize=6,
  kMaxFontSize=72]` and calls it; `resetFontSize()` clears the override
  and rebuilds at whatever `config.ase`'s own `font_size` says.
- `applyConfig()` — called at startup and on every config-file
  hot-reload — now calls `rebuildFont(m_fontSizeOverride > 0 ?
  m_fontSizeOverride : configuredSize)`. This means an active runtime
  zoom **survives** a hot-reload of some unrelated setting (e.g. an
  edited `background` color) rather than snapping back to the file's
  `font_size` on every unrelated save — only `Ctrl+0`, or a restart,
  goes back to the configured value. Editing `font_size` in the file
  itself while a zoom is active has no visible effect until the zoom
  is reset, the same trade-off a browser's own Ctrl+0 vs. its own
  settings page has.

No new config key: this is deliberately a session-only affordance, not
a persisted preference — persisting the zoomed value back to disk
would need deciding *which* file to write and when, out of proportion
to what was actually asked for.

### Vim mode: on by default, and starting in Normal (not Insert)

Reverses ADR 0046's original reasoning ("opt-in, since it changes what
every keystroke does") — the user's now-stated preference is the
opposite: **their** default editing experience should already be
modal. Three places needed the flip to actually stay in sync (the same
lesson ADR 0048 already hit for `font_size`):

- `ase_config_create_default()` (`core/src/config.c`) gains
  `config_set(config, "vim_mode", "true")` — previously missing
  entirely (a pre-existing inconsistency with the template below, only
  ever masked because `ase_config_write_default_if_missing()` always
  wrote the template first).
- `kDefaultConfigTemplate`'s `vim_mode = false` line → `true`, comment
  reworded from "opt-in" to "on by default... set false for plain,
  always-insert editing instead."
- The user's own existing `~/.config/ase/config.ase` (predates
  `vim_mode` entirely, so the key was simply missing and silently
  defaulted to off) — edited directly to add `vim_mode = true`, since
  a *shipped*-default change alone never touches a config file that
  already exists (`ase_config_write_default_if_missing()`'s whole
  point). Read in full before editing, and only that one line added.

Real vim starts a session in Normal mode, not Insert — so enabling Vim
mode without also starting there would mean the "default state" is
still, in effect, plain-Insert editing until the first `Escape`. Fixed
with one new startup-only line in `EditorViewport`'s constructor,
right after `loadConfig()`: `if (m_vimModeEnabled) { m_vimMode =
VimMode::Normal; }`. Deliberately placed in the constructor, not
`applyConfig()` — `applyConfig()` also runs on every config hot-reload,
and doing it there would yank an actively-typing user back to Normal
mode just because `config.ase`'s mtime changed for some unrelated
edit. This is a one-time "how does a session begin" decision, not a
recurring "resync with the file" one.

## Consequences

Verified live: `Ctrl+=` (×4) visibly enlarges text, `Ctrl+-` (×6)
visibly shrinks it below the original, `Ctrl+0` returns to pixel-
identical size with the configured `font_size` — all on the real,
already-existing `~/.config/ase/config.ase` (`font_size = 12`), not a
sandboxed test config. Launching with no `vim_mode` override at all
(the user's real config, freshly edited to add the key) now shows
`NORMAL  Ln 1, Col 1` in the status bar and a visible block cursor on
the very first frame, before any keystroke — both this ADR's default
flip and ADR 0048's immediate-status-label fix holding together
correctly. `ctest --test-dir build` still 9/9.

Anyone else's already-existing config from before this change keeps
whatever `vim_mode` value it explicitly has (this project has exactly
one such config in practice — the user's own, already updated above);
only a config file that never mentioned `vim_mode` at all, generated
fresh from here on, picks up the new on-by-default template.
