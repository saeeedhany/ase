# Your first five minutes

```sh
ase path/to/file.c     # open a file
ase                    # reopen what you had last time
```

Launched with no file, ase reopens the session you closed — the same
files, the same cursor positions. Launched *with* a file it opens that
file and nothing else, because `ase foo.c` means `foo.c`, not "and the
eleven things I had open last week."

## Vim mode is on

By default the editor starts in Normal mode. `i` starts typing, `Esc`
goes back. If that is not what you want, one line in your config turns it
off for good:

```ini
vim_mode = false
```

With it off, ase behaves like an ordinary editor: keys type, `Ctrl+S`
saves, `Ctrl+Z` undoes.

## The six keys worth knowing first

| | |
| --- | --- |
| `F1` | Every shortcut, searchable. The same table this site generates from. |
| `:` | The command line — `:w`, `:q`, `:theme`. Also `Alt+;`. |
| `Ctrl+P` | Open any file in the project by name |
| `Alt+F` | Search the whole project |
| `Ctrl+W` then a key | Move between panes, resize them, close them |
| `Ctrl+S` | Save |

`F1` is the one to remember. It reads the same binding table the editor
dispatches from, so it cannot tell you about a key that does not work.

## Your work survives a crash

Unsaved changes are snapshotted a moment after you stop typing, somewhere
that is not beside your files. If the editor dies — or the machine does —
the next start offers the work back.

That includes a buffer you never named. `Ctrl+N`, type, crash, and it is
still offered when you come back.

Quitting normally is not a crash: work you were asked about and chose to
drop is dropped.

## Where your settings live

`~/.config/ase/config.ase`, written for you on first run. `:config` opens
it in the editor, and saving it applies immediately — there is no reload
command and no restart. See [Configuration](configuration.md).

## Try a theme

```
:theme
:theme simple-nord
:theme save
```

The first lists what is available, the second shows you one, the third
keeps it. Nothing is written to your config until you say `save`.

## Next

- [Configuration](configuration.md) — the file, and what it reloads
- [Keybindings](keybindings.md) — the scheme, and how to change it
- [Vim mode](vim.md) — what is implemented
