# ADR 0113: Keybindings as data

## Status

Accepted

## Context

Every keybinding was a line of C. `docs/EXTENSIBILITY.md` has called this
"the biggest customization gap" since it was written, and ADR 0046
already flagged that building rebinding for Vim alone would be the wrong
shape — the Ctrl chain is not Vim's.

The bindings lived in two chains of `if (event->key() == ...)` inside
`handleCtrlShortcut` and `handleAltShortcut`, plus eight `QShortcut`s in
the window. A third copy of the same knowledge sat in the help panel,
written out by hand.

## Decision

### `key.<chord> = <command>`

Not the `[keys]` section the plan sketched: this config file has no
sections, and it already has two dotted families in `lang.<id>.lsp` and
`filetype.<suffix>`. A third matches what is there.

```
key.ctrl+s = editor.save
key.f5 = editor.compile
key.normal.ctrl+d = vim.half-page-down
key.ctrl+b = none
```

### One registry for built-ins, window actions and plugins

`CommandRegistry` maps a dotted name to a function. The viewport
registers its commands, the window registers the ones that act on the
buffer list, and both go in the same table — which is the point. A user
rebinding Ctrl+S and a plugin bound to a key are then the same
mechanism, and neither needs new API.

### Chords are canonicalised in core

`Ctrl+Shift+F`, `shift+ctrl+f` and `CTRL+SHIFT+F` are one chord, and the
editor has to agree with all three before it can look one up.
`core/src/keymap.c` puts modifiers in a fixed order and lowercases
everything, so the lookup is a string compare and the config file's keys
are the same strings a key press builds.

It is core rather than GUI because it is pure text, and therefore
testable without a window.

### Only chords, never bare keys

A binding needs Ctrl, Alt or Meta, or to be a function key. Shift does
not count, because Shift+A is typing.

Without that rule a user could bind `a` and make the editor untypeable,
and every printable keystroke would take a hash lookup on its way to the
buffer. It also means the dispatch can sit *before* the key switch —
which it must, because that switch claims F1, F12, Escape and the arrows
by key code.

### Vim's takeovers become two rows instead of an `if`

`key.normal.ctrl+d` beats `key.ctrl+d`. The mode is empty when
`vim_mode = false`, so a Vim-only binding simply does not match and a
non-Vim user never sees it — the behaviour ADR 0059 argued for, now
stated as data.

### The help panel reads the same table

It listed the keys by hand, so it would have gone on describing the
defaults after a rebinding — the panel is the user's map of the
keyboard, and a map that lies is worse than none. Each row now asks
which chords run a command, which is also why the reverse lookup
exists. With `key.f2 = editor.open` the panel reads `F2 / Alt+O`.

### A binding that names nothing is reported

Once, when the config is read, in the status bar: `key.ctrl+g: no
command called 'editor.no-such-thing'`. Saying it when the key is
pressed would be too late and too quiet; saying nothing leaves a dead
key looking like a bug in the editor.

## Consequences

Ten parser tests in core, all eight mutations caught, and thirteen
precedence tests in the GUI.

One mutation escaped at first: dropping the check for a trailing `+`.
`"ctrl+"` was already rejected for having no key at all, so the case
that needed the check was `"ctrl+f+"` — a separator after a key, which
without it parses as `ctrl+f`. The test existed for the wrong one of
the two.

`key.*` is not in the project-config allowlist, so a `.ase.conf` in a
repository you cloned cannot bind keys. That falls out of the default-
deny rule from ADR 0087 rather than being added here, which is what
that rule was for.

Two limits:

**Vim's own sequences are not bindable.** `dd`, `gg` and `ciw` are
parsed by a stateful Normal-mode dispatcher, not looked up as chords.
Making those data is a different and larger job, and the plan's own
`normal:dd` example is the part of it left undone.

**Window shortcuts are rebuilt only at startup.** They are `QShortcut`s
so that closing a buffer works while a panel holds focus, and a
`QShortcut` takes its sequence at construction. Editing `key.ctrl+w`
takes effect on the next launch, where the viewport's own bindings
follow the config's hot-reload immediately.
