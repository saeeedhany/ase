# Language servers

ase speaks LSP over stdio. Point it at a server and you get diagnostics,
completion, hover, go-to-definition, find-references and a document
outline.

!!! warning "Linux and macOS only"

    The client spawns the server as a child process over stdio, and that
    path is not implemented on Windows — `ase_process_spawn()` returns
    nothing there, so the server never starts and `:compile` does nothing
    either. See [ADR 0011](../adr/0011-lsp-client.md), decision 6.

## Setting one up

```ini
lsp_command = clangd              # for any language without its own
lang.python.lsp = pylsp           # for one language
lang.rust.lsp = rust-analyzer
lang.go.lsp = gopls
```

The language id is the same name used for highlighting, so a language
with no Tree-sitter grammar still gets a server.

The status bar names the server when it is running, and says so when it
failed or is not configured. A blank bar read as broken, so it is never
blank.

## What you get

| | |
| --- | --- |
| `F12` | Go to definition (`gd` in Vim mode) |
| `Shift+F12` | Every use of the symbol under the cursor |
| `Alt+S` | Outline of this file |
| hover | Hold still over a symbol |
| completion | As you type |
| diagnostics | Underlined, in the theme's own error and warning colours |

Find-references and the document outline open the panel below the buffer.
`Ctrl+W j` moves focus into it, `Ctrl+J` / `Ctrl+K` move through the
results, `Enter` jumps — and keeps the keyboard in the panel, so you can
walk a list of twenty references without reaching back for it each time.

`Ctrl+O` and `Ctrl+I` go back and forward through the jumps you made.

## Per project

One server per project per window, shared by every buffer in it, so
opening a second file in the same repository does not start a second
`clangd`.

The project root is the enclosing git checkout when there is one.

## Suffixes

If a suffix means something other than the default — `.h` in a C++
project, say — tell the project rather than your global config:

```ini
# .ase.conf at the root of the project
filetype.h = cpp
```

That file may only set `filetype.*`, never a command to run. See
[Configuration](configuration.md#per-project-settings).
