# Keybindings

Press `F1` to see every shortcut, searchable. That panel reads the same
table the editor dispatches from, so it cannot tell you about a key that
does not work.

The full table is also on this site:
[Every default binding](../reference/keybindings.md).

## The scheme

Chords are not handed out first-come-first-served. There are five layers,
and which one a binding belongs to follows from what it does.

| Layer | What lives there |
| --- | --- |
| bare keys | Vim's language — `dd`, `ciw`, `w`. Nothing else may take a bare key. |
| `Ctrl+x` | What you bring from other editors and the OS: save, copy, paste, undo, find, quick-open. Plus Vim's own `Ctrl+D`/`U`/`R`/`O`/`I`. A closed set. |
| `Ctrl+W` then a key | Structure: regions, their focus, size and life. Vim's window prefix, with Vim's window keys. |
| `Alt+x` | This editor's own things — panels, pickers, tools. No convention to inherit, so a mnemonic letter. |
| `F<n>` | Ask the language server. `F12` and `Shift+F12` are what every editor uses. |

The reasoning, and what it replaced, is
[ADR 0120](../adr/0120-a-law-for-the-keyboard.md).

### The `Ctrl+W` prefix

Press `Ctrl+W` alone and the status bar shows what it is waiting for:

```
Ctrl+W →  C  =  J  K  -  N  O  +  Q  W
```

`j` and `k` move focus between the buffer and the panel below it, `w`
cycles, `c` closes whichever region has focus, `+` and `-` resize, `o`
leaves only the editor. `Escape` cancels.

A prefix that swallows a keystroke and shows nothing is indistinguishable
from a dropped key, which is why it tells you.

The prefix is caught application-wide rather than by the editor widget.
The whole point is reaching the editor *from* the panel, and a key only
the editor can hear cannot be the way back to it.

## Changing one

In your config, `key.<chord> = <command>`:

```ini
key.ctrl+s = editor.save
key.alt+g  = editor.find-in-project
```

The command names are listed in
[Every command](../reference/commands.md). A binding pointing at a name
that does not exist is reported in the status bar when you save the
config — it does not fail silently.

### Only in one Vim mode

Put the mode before the chord:

```ini
key.normal.ctrl+d = vim.half-page-down
key.insert.ctrl+d = editor.cursor.add-next-occurrence
```

A mode-qualified binding beats an unqualified one, and your config beats
the defaults — in that order. So you can rebind `Ctrl+D` everywhere
without disturbing Vim's use of it, or the other way round.

### Two-chord sequences

`>` separates the halves:

```ini
key.ctrl+w>t = editor.compile
```

Not `.`, which is already the mode separator, and not `,`, which is a key
you might want to bind.

### Turning a default off

```ini
key.ctrl+d = none
```

`none` unbinds. For a sequence it unbinds that one continuation; the
chord stays a prefix for the others.

## Writing a chord

Lowercase, `+` between parts, modifiers first: `ctrl+shift+s`,
`alt+left`, `f12`, `ctrl+w>j`.

Punctuation is spelled out — `semicolon`, `minus`, `plus`, `equal`,
`comma`, `period`, `slash`, `backslash`, `apostrophe`, `bracketleft`,
`bracketright`, `grave`. Named keys are `escape`, `tab`, `return`,
`backspace`, `delete`, `space`, `left`, `right`, `up`, `down`, `home`,
`end`, `pageup`, `pagedown`, `insert`.

## Plugin commands

A command a plugin registered can be bound like any other:

```ini
key.alt+u = uppercase_all
```

See [Writing a plugin](plugins.md).
