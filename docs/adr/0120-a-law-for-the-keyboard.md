# ADR 0120: A law for the keyboard

## Status

Accepted

## Context

Chords had been added one at a time, each taking whatever was free.
`Ctrl+B` compiled, `Ctrl+;` opened the command line, `Ctrl+Shift+.`
listed symbols. Nothing said where a *new* binding should go, so each
new one was arbitrary too, and `Ctrl+Shift+` had become a junk drawer
with six unrelated bindings in it.

Worse, nothing addressed **regions**. The editor and the docked panel
are two places the keyboard can be, and there was no vocabulary for
moving between them. `Escape` left the panel and nothing went back —
reported as "I click a reference and then cannot get back to the panel".

ADR 0118 patched that with a three-state toggle. A toggle that has to
guess is a sign the model is missing, not a model.

## Decision

Five layers, each with a reason rather than a free slot.

| layer | what belongs there | why |
|---|---|---|
| bare keys | Vim's language — `dd`, `ciw`, `w` | the buffer is Vim's, and nothing else may take a bare key |
| `Ctrl+x` | what you bring from other editors and the OS: save, copy, cut, paste, undo, find, quick-open — plus Vim's own `Ctrl+D`/`U`/`R`/`O`/`I` | muscle memory that predates this editor. A closed set; nothing new goes here |
| `Ctrl+W` then a key | structure: regions, their focus, size and life | Vim's window prefix, with Vim's window keys. `Ctrl+W j` already means "the window below" to the people this editor is for |
| `Alt+x` | this editor's own things — panels, pickers, tools | no cross-editor convention to inherit, so a mnemonic letter is the best available |
| `F<n>` | ask the language server | `F12` and `Shift+F12` are already what every editor uses |

`Ctrl+W c` closes whichever region has focus, which is Vim's meaning and
absorbs the old bare `Ctrl+W` (close buffer) rather than losing it.

### The prefix is caught application-wide

An event filter on the application, not a handler on the viewport. The
whole point is reaching the editor *from* the panel, and a key only the
editor can hear cannot be the way back to it. The same filter is why
the prefix works while a find bar or the file browser holds focus.

### The prefix says what it is waiting for

Pressing `Ctrl+W` alone puts `Ctrl+W →  C  =  J  K  -  N  O  +  Q  W`
in the status bar. A prefix that swallows a keystroke and shows nothing
is indistinguishable from a dropped key, and it is how tmux's own prefix
is learned by everyone who uses it.

`Escape` cancels, and the Escape is spent doing so.

### `>` separates the two chords

`key.ctrl+w>j = pane.focus-down`. Not `.`, which is already the mode
separator (`key.normal.ctrl+d`), and not `,`, which is the comma key.

## Consequences

Everything moved at once rather than being aliased, which is a real cost
paid deliberately: `Ctrl+Shift+F` is now `Alt+F`, `Ctrl+B` is `Alt+B`,
`Ctrl+;` is `Alt+;`, `Ctrl+Shift+.` is `Alt+S`, and `Ctrl+Shift+O`,
`Ctrl+Shift+Up`/`Down` and bare `Ctrl+W` are gone into the prefix. Half
measures would have left the junk drawer in place under a second name,
and the law is only worth having if the table actually obeys it.

Every binding remains data, so anyone who wants the old chords back
writes them into `config.ase` — which is the point of ADR 0113 and the
reason this was safe to do in one pass.

Verified by driving the editor: `Alt+F` opens project search, `Ctrl+W`
alone lists its keys, `Ctrl+W j` moves focus into the panel where
`Ctrl+J` then navigates, `Ctrl+W -` shrinks it, and `Ctrl+W c` closes
the panel with the program still running.

The three-state toggle from ADR 0118 stays on `Alt+P`. It is still the
right thing for one key that means "the panel", and it is no longer
carrying the whole weight of region navigation.
