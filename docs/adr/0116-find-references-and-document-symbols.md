# ADR 0116: Find references and document symbols

## Status

Accepted

## Context

Go-to-definition landed in ADR 0067 and the jumplist in ADR 0070, so a
name could be followed to where it is defined and walked back from. The
other direction — who uses this — was missing, and the roadmap had it as
the largest remaining functional gap for anyone navigating a real
codebase.

## Decision

### Two requests on the existing client

`textDocument/references` and `textDocument/documentSymbol`, added
alongside definition, completion and hover. References carries a
`context.includeDeclaration`, which is the only structural difference:
without it servers disagree about whether the declaration is one of the
results, and "who uses this" reads better with it than without.

The fake LSP server answers both, with two locations in *different*
files and a symbol tree with a child, so a caller that reads only the
first result or only the top level is caught by the core tests.

### Results go in the output panel, which already does this

Project-wide search already shows a list of `path:line: text` that jumps
on Enter. References are the same thing arrived at differently, so
`showSearchResults` was split: `showLocations` renders the list under a
summary the caller writes, and the search wording moved into its own
caller. No new panel, and the keyboard behaviour is the one already
learned.

Document symbols use the same list, indented by nesting depth, with a
short kind prefix (`fn`, `struct`, `field`) for the kinds worth telling
apart at a glance. The rest are unlabelled rather than guessed at.

### Both result shapes, for both requests

`documentSymbol` answers either as a flat `SymbolInformation` array,
where the range hides under `location`, or as a nested `DocumentSymbol`
tree, where it does not. References answer as `Location` or
`LocationLink`. clangd picks one of each; reading only clangd's would
work until it did not, which is the same reasoning ADR 0067 gave for
definition.

### The line text is read from disk

A list of coordinates is not a list of results. Each referenced line is
read for context, grouped by file so a file with twelve hits is read
once.

## Consequences

Verified against real clangd on this repository, not only the fake
server: 49 references to `ase_buffer_insert` across six files, and 43
symbols in `buffer.c`. Selecting a row opened `undo.c` at line 222 with
the caret on column 13, which is the symbol.

**This found a real gap in ADR 0113.** `F1` and `F12` were still
hardcoded in `keyPressEvent` *above* the binding dispatch, so they never
reached the table that claims to own every chord. Rebinding either did
nothing, and `Shift+F12` arrived as plain `F12` — which is how this was
noticed, when find-references went to the definition instead. Both are
ordinary rows now. Anything else claiming a key before the dispatch
would have the same problem; these two were the only ones left.

LSP counts characters in UTF-16 code units and this treats them as
bytes. Identical for ASCII, which identifiers overwhelmingly are; on a
line with wider characters before the symbol the caret lands a little
early, which is recoverable in a way a wrong line would not be.

**Rename is deliberately not here.** `textDocument/rename` returns a
WorkspaceEdit touching files that are not open, and applying it wants a
preview and an undo story that spans buffers — the same design pass
replace-across-files is still waiting for. Doing it as a third request
on this ADR would be doing the easy half of a hard feature.
