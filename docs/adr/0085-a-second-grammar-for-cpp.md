# ADR 0085: A second grammar for C++

## Status

Accepted

## Context

[ADR 0007](0007-syntax-highlighting-tree-sitter.md) added Tree-sitter
with exactly one grammar, tree-sitter-c, and a hand-written query over
it. `ase_syntax_create_c()` said so in its name.

The editor already claimed more than that. The language-server client
sends `cpp` as the `languageId` for C++ suffixes
([ADR 0063](0063-language-server-state-in-the-status-bar.md)), so
opening a `.cpp` file gets diagnostics and `gd`
([ADR 0067](0067-go-to-definition.md)) from clangd. Highlighting was
gated separately, on `.c` and `.h` only, and silently did nothing.

So a C++ file had working diagnostics and no colour at all. Two gates
for one question, disagreeing.

## Decision

### The C grammar is not good enough for C++

The cheap fix is to point the existing grammar at C++ suffixes. It was
tried and measured against a file using namespaces, templates, `auto`,
`class`, access specifiers and a raw string. Roughly 60% of the tokens
came out right — the shared C subset — and the rest split two ways:

- **missing**: `namespace`, `template`, `typename`, `class`, `public`,
  `private`, `explicit`, `override`, `nullptr` and the rest of the C++
  keyword set are plain identifiers to a C parser.
- **wrong**: worse than missing. `auto n = f();` coloured the `n` as a
  type, and `T &x` coloured the `x` as one, because the C parser
  recovers from what it cannot parse by guessing declarators. The
  editor was not failing to highlight C++, it was highlighting it
  incorrectly.

Missing colour reads as "this language is not supported yet". Wrong
colour reads as a bug in the editor, and is actively misleading about
what the code says. That is what settles it.

### `ase_syntax_create(AseLanguage)`

The constructor takes an enum instead of naming a language in the
symbol:

```c
typedef enum {
    ASE_LANG_C,
    ASE_LANG_CPP,
} AseLanguage;

AseSyntax *ase_syntax_create(AseLanguage language);
```

The body is a `switch` over a static helper that takes a `TSLanguage *`
and query bytes; adding a third language is one case and one `.scm`.
The enum is deliberately not a string or an open registry — see the
cost below.

### Every node name is checked against `node-types.json`

`ts_query_new` rejects the whole query if any single node name in it is
unknown to the grammar, and `syntax_create` then returns `NULL`. In the
editor that failure looks exactly like "this language has no colours" —
there is no diagnostic, no log line, nothing to distinguish a typo in
the query from a language nobody added yet. It is the same silent
failure this ADR exists to remove, reintroduced one layer down.

So each name in `cpp_highlights.scm` was checked against the grammar's
own `node-types.json` before it was written. That turned up things
guessing would not have:

- `auto`, `this` and `null` are **named** nodes; `nullptr` is anonymous
  and must be quoted as a literal.
- `static_cast`, `dynamic_cast`, `const_cast`, `reinterpret_cast`,
  `export` and `import` are **not matchable at all** — the grammar does
  not give them nodes of their own. Upstream's own query does not
  highlight them either. They stay uncoloured rather than being faked.

`test_syntax.c` now asserts `ase_syntax_create(ASE_LANG_CPP) != NULL`
and checks a few keyword and comment captures, so a bad node name fails
the build rather than shipping as a blank file.

## Consequences

**The binary grows by 160%.** Measured, release build, before and
after:

| | before | after |
|---|---|---|
| `ase_gui` | 2097.9 KB | 5464.9 KB |
| `libtree_sitter_c_grammar.a` | 620.6 KB | 620.6 KB |
| `libtree_sitter_cpp_grammar.a` | — | 3377.6 KB |
| full clean build | 16.6 s | 17.2 s |

Stripping the result recovers 158 KB of the 3367 KB. The weight is
parse tables — data, not symbols — so there is no link-time or
packaging trick that gets it back. tree-sitter-cpp's generated
`parser.c` is 16.9 MB of source against tree-sitter-c's 3.7 MB, and the
binary reflects that ratio. Build time is the thing that does *not*
suffer: +0.6 s.

**This is the argument for keeping the language list curated.** A
config-driven language server costs a line of configuration per
language, because the server is someone else's process — if you can
name a command, it works. A config-driven *highlighter* cannot work the
same way: every language is a compiled-in grammar plus a query somebody
has to verify against it, and the second one alone tripled the binary.

So the two gates stay different on purpose, and now say so:

- **LSP** — can become config-driven. A language works if you configure
  a command for it.
- **highlighting** — a short enum. Each entry is paid for in megabytes,
  so each one is a decision.

They will still disagree about specific files, just no longer silently.

**`.h` is still assumed to be C**, by both gates. That is wrong for the
many C++ projects that use `.h` for headers: such a file gets the C
grammar's mis-colouring described above and a `c` `languageId`. The
suffix alone cannot answer this — vim guesses from content and gets it
wrong too. It wants a per-project override, which belongs with the
configuration work rather than here.
