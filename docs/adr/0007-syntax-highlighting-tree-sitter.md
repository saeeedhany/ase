# ADR 0007: Syntax highlighting via Tree-sitter, monochrome by design

## Status

Accepted

## Context

Spec section 4 already settled *that* syntax highlighting uses
Tree-sitter; this ADR covers the choices Phase 3 had to make to actually
wire it in, and resolves a real tension in the spec: the "Aesthetic"
pillar demands "one background color, one font color" as the whole
palette, while section 7 phase 3 demands working syntax highlighting.
Traditional syntax highlighting means different hues per token kind —
seemingly a direct contradiction.

## Decisions

### 1. One hue, differentiated by weight/style/opacity, not color

Highlighting is real (Tree-sitter parses and classifies every token),
but the **default theme never introduces a second hue**. Every capture
category is rendered in the same base text color
(`#F5E6C8`), varied only by:

| Capture | Treatment |
|---|---|
| plain text | full opacity, regular weight |
| keyword | full opacity, **bold** |
| type | full opacity, *italic* |
| string / number | ~78% opacity |
| comment | ~45% opacity (most de-emphasized — matches the "everything
  else is de-emphasized, not competing for attention" rule for
  secondary UI, extended here to secondary *text*) |

This is what makes "one font color" and "syntax highlighting" both true
at once: color identity is preserved (a keyword is structurally
distinguishable from a string in the data the syntax module returns),
but the *rendering* stays monochrome. A future theme (Phase 4) can remap
these captures to actual hues if a user wants that — the capture
categories (`keyword`/`type`/`string`/`number`/`comment`/none) are
theme-agnostic; only `gui/src/editor_viewport.cpp`'s current hardcoded
mapping to opacity/weight is the default-theme choice, not a limitation
of the syntax module itself.

### 2. Tree-sitter fetched via CMake `FetchContent`, not vendored

`modules/syntax/CMakeLists.txt` pulls `tree-sitter` (the runtime) and
`tree-sitter-c` (the one grammar v1 supports) from GitHub at configure
time, pinned to exact tags (`v0.27.0` / `v0.8.0`). Vendoring the
generated sources directly into this repo was considered — it would
make builds work with zero network access — but was rejected for v1:
it means committing large generated/third-party C files
(`parser.c` amalgamations) that this project doesn't maintain, blurring
what's ours vs. upstream, for a build-time cost (one clone, cached
after) that every other CMake `FetchContent`-based C/C++ project already
accepts. Revisit if offline/air-gapped builds become a real requirement.

### 3. Exactly one language in v1: C

Language support is chosen by file extension (`.c`/`.h` → C; anything
else renders as plain, unhighlighted text — never a crash or wrong
highlighting, just none). C was picked because it's the language this
project's own core is written in, so opening `core/src/buffer.c` in the
editor itself is the natural first test. Each additional language is,
by construction, one more grammar fetched behind the same
`AseSyntax`/`ase_syntax_highlight` interface — nothing else in the
module needs to change shape to add Python, C++, Rust, etc. later.

### 4. Highlight query is hand-authored, not the grammar's own `highlights.scm`

`modules/syntax/src/queries/c_highlights.scm` is written for this
project — a small, deliberately minimal set of captures (keyword,
comment, string, number, type) — rather than pulled from
`tree-sitter-c`'s own `queries/` directory (intended for editors like
Neovim with a much larger, editor-specific capture vocabulary). Keeps
the capture set exactly as large as what the default theme in decision
#1 actually uses, and decouples us from that file's location/format
across grammar versions.

### 5. Full reparse on every edit, no incremental `ts_tree_edit` yet

Tree-sitter's headline feature is incremental reparsing (feed it the
previous tree plus a small edit, get an updated tree cheaply). v1 does
a full `ts_parser_parse_string` over the whole buffer on every
edit — the same shape of shortcut as the GUI's full-buffer mirror
(ADR 0006), and for the same reason: correctness first, and both should
be fixed together later, since incremental *parsing* only pays off once
the GUI is also feeding it incremental, windowed *reads* rather than the
whole buffer. Tracked in `docs/ROADMAP.md`.

One consequence measured directly: `AseSyntax` keeps one long-lived
`TSParser`/`TSQuery`/`TSQueryCursor` (the standard, documented
tree-sitter usage pattern — recreating a `TSParser` per call was tried
and rejected, it crashes when combined with a reused cursor). Calling
`ts_parser_parse_string` with `old_tree = NULL` repeatedly on unrelated
content, as full-reparse-every-edit does, grows RSS noticeably before
plateauing (an internal tree-sitter cache saturating, not an unbounded
leak — verified by running 200k parses of the same small string to
completion under ASan/LeakSanitizer: RSS climbs then holds flat, no
leak report). Not a correctness bug, but another reason incremental
parsing is worth doing before large/long-running-session use: it avoids
feeding the parser a stream of "unrelated" full-buffer reparses in the
first place.

## Consequences

Adding a language later is additive (new grammar + new `.scm` + a
file-extension check), not a redesign. The perf ceiling here is the
same whole-buffer-per-keystroke ceiling as ADR 0006 — fine for the
source-sized files this phase is tested against, wrong for "several
hundred MB" files, and both need fixing in the same pass.
