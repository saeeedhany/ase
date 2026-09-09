# ADR 0008: Config/theme format is a minimal hand-rolled key=value format, not TOML

## Status

Accepted

## Context

Spec section 4 left the config/theme format open between TOML and "a
minimal custom format," to hot-reload, with theme and config sharing one
format. `docs/ROADMAP.md` tracked this as an open decision blocking
Phase 4, along with the exact default text color (spec section 2:
approx. `#F5E6C8`, "TBD, to be tuned by eye").

## Decisions

### 1. Minimal `key = value` format, not TOML

The entire v1 config surface is four flat scalars: two colors, a font
family, a font size (see `core/src/config.c`'s defaults). Pulling in a
TOML parser (a third external dependency, after Qt6 and Tree-sitter) to
parse four flat key-value pairs doesn't earn its complexity budget —
TOML's actual value (tables, arrays, typed literals, nesting) has no
user here yet. The hand-rolled parser is about 60 lines: skip blank
lines and `#` comments, split on the first `=`, trim whitespace. Fully
auditable, zero dependencies, matches the "Minimal" pillar directly.

**Revisit this if the config surface grows structure** — e.g. per-language
theme overrides, keybinding tables, or plugin settings with real nesting
would outgrow a flat format fast, and that's the point at which TOML's
complexity starts being worth paying for. Nothing in `AseConfig`'s API
(`core/include/ase/config.h`) assumes flat-forever; swapping the parser
later doesn't change the public shape (string/int/color lookups by key).

### 2. Config and theme are the same file, same format

Exactly as the spec asked: one `config.ase` holds both editor settings
(`font_family`, `font_size`) and theme colors (`background`, `text`).
No separate theme-file concept in v1 — there's nothing yet that would
motivate having more than one theme active at a time.

### 3. Default text color ships as `#F5E6C8`, "tuned by eye" becomes a
config edit, not a guess

The spec flagged the exact accent color as TBD, "to be tuned by eye" —
work that requires a human looking at a rendered screen, not something
to guess numerically. Phase 4 resolves this differently than by picking
a "final" number: it ships `#F5E6C8` (the spec's own approximation) as
the default, and the config system itself — hot-reloaded — is the
mechanism for tuning it. Open the editor, edit `background`/`text` in
`config.ase`, save, see the change within `ASE_CONFIG_POLL_MS`
milliseconds, no restart. This closes the ROADMAP open-decision item by
making it a five-second edit instead of a hardcoded constant.

### 4. Hot-reload via mtime polling, not OS file-watch APIs

The GUI checks the config file's modification time on a timer
(750ms, `EditorViewport`) rather than using inotify/FSEvents/
`ReadDirectoryChangesW`. Real file-watching APIs are meaningfully
platform-specific code for a file that's read maybe once every few
seconds at most — polling a stat() is negligible overhead and identical
across Linux/macOS/Windows. Revisit only if profiling ever shows this
timer costing something measurable (it won't, for one small file).

### 5. Missing config file: ship in-memory defaults, best-effort write a
starter file

`ase_config_load` never fails outright — a missing or malformed file
just means the built-in defaults apply (see decision 3's values). On
first run, `ase_config_write_default_if_missing` best-effort writes a
commented starter file to the resolved path
(`$XDG_CONFIG_HOME/ase/config.ase`, falling back to `~/.config/ase/config.ase`
on Unix; `%APPDATA%\ase\config.ase` on Windows) so there's something
discoverable to edit — but only creates the immediate `ase/` directory,
not any missing parent (`~/.config` itself). If that parent doesn't
exist, the write silently no-ops and the editor keeps running on
in-memory defaults. Acceptable: `~/.config` existing is near-universal
on any system that's run other XDG-aware software, and the failure mode
is "no starter file," never a crash or wrong behavior.

## Consequences

Theming beyond "one background, one text color, one font" (e.g. the
syntax highlighting capture styles from ADR 0007) stays hardcoded in
`EditorViewport` for now — not yet exposed as config keys. Natural
follow-up once someone actually wants to retune keyword-bold vs.
comment-opacity without recompiling; tracked in `docs/ROADMAP.md`, not
blocking this phase.
