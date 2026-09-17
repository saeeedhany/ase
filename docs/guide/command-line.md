# The command line

`:` in Normal or Visual mode, or `Alt+;` anywhere — including from a
panel, so you never have to go back to the buffer first just to type
`:q`.

`/` and `?` open the same bar for search.

| | |
| --- | --- |
| `:w` | Save |
| `:q` | Close. With a panel open it closes the panel first — that is what you are looking at, and closing the buffer underneath it is the bigger, less recoverable action. |
| `:q!` | Close, discarding changes. Deliberately skips the panel rule: it is the escape hatch. |
| `:42` | Go to line 42 |
| `:s/from/to/` | Substitute on this line. `/g` for every match, `/i` to ignore case. |
| `:compile` | Run `build_command` and show the output |
| `:output` | Show or hide the output panel |
| `:config` | Open your config file |
| `:theme` | List the themes; the current one is in `[brackets]` |
| `:theme <name>` | Apply it now, without writing anything |
| `:theme save` | Keep the applied theme |
| `:<plugin command>` | Run a command a plugin registered |

An unrecognised name reports `unknown command: <name>` rather than doing
nothing quietly — silence made a typo look like a command that ran.

## Any command by name

Anything in [Every command](../reference/commands.md) can also be typed
here — `:editor.find`, `:pane.close`, `:buffer.next` — resolved exactly
as a key binding resolves it, so a name means the same thing however you
reach it.

Plugin command names work the same way.
