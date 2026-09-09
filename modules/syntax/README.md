# Syntax highlighting module

Tree-sitter-backed highlighting, currently for C only. Public API in
`include/ase/syntax.h`; see
[`docs/adr/0007`](../../docs/adr/0007-syntax-highlighting-tree-sitter.md)
for why it's built this way (one language in v1, hand-authored query,
`FetchContent`-fetched dependencies, and the default theme's
one-hue-only rendering rule).

- `src/syntax.c` — the module itself.
- `src/queries/c_highlights.scm` — the highlight query (source of
  truth; embedded into the binary at configure time).
- `tests/` — unit tests (`-DASE_BUILD_TESTS=ON`, the default).
- `fuzz/` — libFuzzer harness on the parser boundary
  (`-DASE_BUILD_FUZZERS=ON`, requires clang).

Adding a language means: fetch its grammar in `CMakeLists.txt`, write a
query for it, and add a creation function alongside
`ase_syntax_create_c` — nothing else in the module's shape changes.
