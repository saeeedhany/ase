# ADR 0132: Renaming a symbol

## Status

Accepted

## Context

The last of the language server's useful half. It waited on
[ADR 0131](0131-replacing-across-files.md), because a rename and a
project-wide replace want the same two answers — what does undo mean,
and does anything touch disk — and answering them twice differently
would have been the wrong shape.

With those settled, rename is a second **producer** of an edit set the
preview and apply path already take. What was left was the protocol and
the reading of a `WorkspaceEdit`.

## Decision

`F2`. The `F<n>` layer is "ask the language server"
([ADR 0120](0120-a-law-for-the-keyboard.md)), and `F2` is what every
other editor binds rename to, which is the whole argument for the layer.

The prompt is the find bar with one field, prefilled with the name under
the caret, so the common edit is a few keystrokes rather than retyping
it.

### The core sends and does not interpret

`ase_lsp_client_request_rename()` matches every other request in that
header: it sends, and hands the reply back as raw JSON. Nothing is
edited there. A rename to an empty name is refused before anything is
sent.

### Reading a WorkspaceEdit is its own unit

`lsp::replacementsFrom()` in `gui/src/lsp_workspace_edit.h`, because it
is the part with edge cases rather than the part with plumbing:

- Two shapes. `changes` is an object keyed by file URI; `documentChanges`
  is an array of `{textDocument, edits}`. Servers pick, so both are read.
  Reading only one works until a server that picks the other is used.
- A **multi-line range is dropped, not approximated.** Rename does not
  produce one for a name, and guessing at what a server meant by one is
  how a refactor eats a function body.
- A URI that is not a local file — a built-in, something inside an
  archive — is skipped.
- An entry with no range, no `newText`, or an empty range is skipped.

Iterating `changes` needed something the JSON layer did not have: every
accessor took a key, and these keys are data. `ase_json_object_size()`,
`ase_json_object_key()` and `ase_json_object_value()` are new.

### Every edit says what must already be there

LSP counts characters in UTF-16 code units; this editor reads bytes.
[ADR 0067](0067-go-to-definition.md) accepted that for a caret, where
being a little early on a line of wide characters is recoverable. For a
rename it is not: a wrong *length* eats whatever is next to the name.

So `TextEdit` gained `expected` — the bytes that must already be at that
position — and an edit whose bytes do not match is skipped. That closes
the same hole for replace, where the file may have changed between the
search and the apply, and it is what makes an edit checkable rather than
trusted.

## Consequences

`project::Replacement` now carries its own length and replacement text
rather than the operation carrying one of each. A `WorkspaceEdit` need
not use the same length or the same text everywhere, and search-and-
replace is the special case where it does.

Verified against real clangd, not only the fake server: renaming
`widget_total` across a header and an implementation produced four
changes in two files, including both occurrences on one line, and applied
them correctly.

That run also caught the preview drawing the same picture for both of
those occurrences. `textColumn` was being found with `indexOf`, which
always finds the first one. It is now derived from how much indentation
was trimmed, so each row previews its own change.

The docgen guard from [ADR 0126](0126-the-docs-read-the-tables.md)
refused the build when `F2` was added: its "function keys" layer matched
`c.contains("f1")`, which claimed `shift+f12` by accident and rejected
`f2` outright. The matcher now tests the key rather than a substring.
It refused to ship a reference page that was quietly one row short,
which is what it was written to do.

What rename still does not do: a server that answers with a
`documentChanges` carrying file creations, deletions or renames has
those ignored — only text edits are read. Nothing in C or C++ produces
them for a symbol rename.
