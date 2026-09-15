# ADR 0086: One language gate, and per-language config keys

## Status

Accepted

## Context

[ADR 0085](0085-a-second-grammar-for-cpp.md) fixed C++ highlighting and
named the thing it did not fix: the editor decided what language a file
was in **three separate places**, from two copies of the same suffix
table.

- `editor_viewport.cpp` mapped a suffix to a grammar.
- `lspLanguageIdFor()` in `editor_viewport_lsp.cpp` mapped a suffix to
  an LSP `languageId`.
- `lsp_command` was one global string for every language at once.

Two tables drift. That is exactly how a `.cpp` file ended up with
diagnostics and no colour for as long as it did — one table was updated
and the other was not, and nothing connects them, so nothing said so.

The third is a different problem: one `lsp_command` cannot serve a
machine with both clangd and rust-analyzer installed.

## Decision

### The resolver moves into core, and there is one of it

```c
const char *ase_config_language_for_path(const AseConfig *config, const char *path);
```

It returns a language name — `"c"`, `"cpp"` — which is *also* the LSP
`languageId`, because those were never two things. Both gates call it.
The duplicate table is gone, and `lspLanguageIdFor()` with it.

A language the resolver names but no grammar handles still highlights
nothing. The difference is that it is now the same answer the LSP gate
got, rather than a second opinion.

### Structure by key convention, not by nesting

[ADR 0008](0008-config-theme-format.md) chose a flat `key = value`
format and said to revisit it "if the config surface grows structure —
per-language theme overrides, keybinding tables". This is that moment,
and the answer is that it does not need revisiting yet:

```
lang.cpp.lsp = clangd
filetype.h = cpp
```

A dotted key is still a flat key. `lang.cpp.lsp = clangd` splits on the
first `=` and lands in the existing store unmodified — the parser did
not change, no TOML dependency appeared, and the namespace is carried
by the key rather than by the file format. Two lookup helpers are the
entire addition.

This holds as long as values stay scalar. A keybinding table with real
nesting would still outgrow it, and ADR 0008's advice still stands for
that day.

### `filetype.<suffix>` overrides what a suffix means

The built-in table is the same one both gates used. Config overrides it
per suffix, which is what `.h` needed: it is C by default because a C++
header parsed as C fails loudly rather than silently, but that default
is wrong for any C++ project that uses `.h`, and the suffix alone
cannot tell you which you have.

The override sets the language for *both* gates at once, which is the
point of there being one resolver. Verified on a C++ header:
`explicit`, `virtual` and `override` were coloured as **types** under
the C grammar — the mis-parse ADR 0085 measured — and are keywords
under `filetype.h = cpp`.

It also accepts languages the editor has no grammar for. With
`filetype.rs = rust` and `lang.rust.lsp` set, a `.rs` file gets a
language server and no highlighting — tested. That is the split ADR
0085 argued for, now actually true rather than merely intended:

- **LSP** — config-driven. Name a command, it works, for any language.
- **highlighting** — a short enum, because each entry costs megabytes.

### `lsp_command` still works

It is the fallback when the language has no `lang.<id>.lsp` of its own,
so existing config files keep working and a single-language setup stays
a single line. Both paths were checked against a running clangd.

## Consequences

`filetype.*` is a user-level setting, so `filetype.h = cpp` applies to
every project at once — including the C ones, where it is now wrong in
the other direction. The setting that would actually fit is per-project,
and this ADR does not add one.

That is deliberate rather than deferred by accident. A project-local
config file read from the repository means a cloned repository can
choose what the editor does, and `lsp_command`/`build_command` make
that arbitrary process execution on open — vim's `exrc` problem, which
vim answers with `secure` and neovim with a trust prompt. The design
that avoids needing either is to let project config set what files
*mean* and never what commands to *run*, which splits cleanly along the
lines these keys already fall on: `filetype.*` describes the repository,
`lang.*.lsp` describes the machine it is checked out on. Worth building;
worth building deliberately, with that rule decided first.
