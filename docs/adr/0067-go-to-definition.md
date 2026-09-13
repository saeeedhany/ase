# ADR 0067: Go to definition

## Status

Accepted

## Context

Diagnostics (ADR 0029), completion and hover (ADR 0030) were wired to
the language server. Navigation was not — and navigation is the half
people actually miss, because reading code is mostly following names to
where they are defined.

`ase_lsp_client_request_definition()` has existed in `core/` since
ADR 0011, tested, with nothing in the GUI ever calling it. The same
situation the plugin host was in before ADR 0054: the capability was
built and then left unreachable.

## Decision

### Two bindings, because there are two audiences

`gd` in Normal mode — vim's own key for this, in an editor that defaults
to vim mode — and `F12`, which is what every other editor uses and works
in any mode, including with `vim_mode = false`.

`gd` slots into the existing pending-`g` state next to `gg`. It is not a
motion, so no operator can be pending against it: `dgd` is not a thing,
and it jumps and that is all.

### Every way it can fail says so

Completion and hover are *offered* — they appear when they have
something and stay quiet when they don't, which is right for something
you didn't ask for. Go-to-definition is **asked for**, on a keystroke,
so silence reads as a broken key. Each case gets a message
(ADR 0062):

- no server for this file → `no language server for this file`
- server said nothing, or an empty array → `no definition found`
- a non-`file:` URI (a built-in, something inside an archive) →
  `definition is not in a file`
- the server returned an error → the server's own text

This is the first feature built after the message line existed, and it
is noticeably better for it: the old alternative was a key that
sometimes did nothing for four different reasons.

### Three response shapes, because servers choose

`textDocument/definition` may answer with a single `Location`, an array
of `Location`, or an array of `LocationLink` — which names its target
`targetUri`/`targetSelectionRange` rather than `uri`/`range`. clangd
returns the array form; handling only that would work until the day
someone points `lsp_command` at a different server.

Multiple results take the first. For C, "several definitions" is
usually a declaration and its definition, and the server lists the one
you want first. A picker is deferred rather than guessed at.

### Jumping is still two objects' work

A definition in another file is a path *and* a line, exactly like a
search hit (ADR 0066) — so the viewport emits
`fileOpenAtLineRequested(path, line)` and the window opens the buffer
and calls `goToLine()`. A definition in the *same* file skips that
entirely and just moves the cursor, rather than asking the window to
"open" a file already on screen.

### The language gate had to grow, and that was overdue

The server was started only for `.c` and `.h`, so `gd` could not work on
this editor's own C++ sources — a strange thing to ship. clangd handles
both languages; the gate was never a decision, just a list from ADR 0029
that never grew. It now covers `.cpp/.cc/.cxx/.hpp/.hh/.hxx` as well.

The language id sent on `didOpen` matters and is now derived rather than
hardcoded to `"c"`: telling a server `c` about a `.cpp` file makes it
parse C++ as C, and the resulting errors look like your code is broken
rather than like the editor lied. `.h` stays `c` — ambiguous by nature,
and a C++ header parsed as C fails loudly rather than silently, while
the compile database corrects it when there is one.

Tree-sitter highlighting is a separate gate and stays C-only: that one
needs a grammar per language, this one does not.

## Consequences

Verified live against clangd, with `build/compile_commands.json`
present:

- **In-file**: `gd` on `lspDefinitionTrampoline` at line 464 of
  `editor_viewport_lsp.cpp` lands on line 69, where it is defined.
- **Cross-file**: `gd` on `ase_lsp_client_request_definition` opens
  `core/src/lsp_client.c` as a new buffer at line 561 — its definition.
- **F12**, on `bool`, opens `stdbool.h` at line 24: a different binding,
  a file outside the project entirely, and proof the jump does not
  assume the target is in the workspace.
- Before the language gate was widened, `gd` in a `.cpp` file reported
  `no language server for this file` — which is how the gate was found.

`ctest` 9/9, clean build, zero warnings.

Deferred, and the first one matters:

- **No way to jump back.** Vim has a jumplist on `Ctrl+O`; here `Ctrl+O`
  is Open, and ADR 0046 already flagged that collision when it deferred
  the jumplist. Following a definition into a file you did not choose,
  with no way back except the buffer bar, is the obvious next gap.
- **A picker for multiple results**, which wants the same list-in-the-
  output-panel shape project search just built.
- **Find references and rename**, the rest of LSP's navigation half.
