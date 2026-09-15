# ADR 0103: The mainstream languages

## Status

Accepted

## Context

[ADR 0086](0086-one-language-gate-and-per-language-keys.md) split the
two gates and claimed the result: *"LSP — config-driven. Name a command,
it works, for any language."*

It did not. `ase_config_language_for_path` knew eight suffixes, all C and
C++, and returned NULL for everything else — which
`startLspClientIfConfigured` turned into `NotApplicable`, a **completely
blank** indicator. A `.py` file with `lang.python.lsp` correctly set
looked exactly like a `.txt` file. Nothing said that a second line,
`filetype.py = python`, was also required.

So the promise was true only for languages the editor already knew, of
which there were two.

## Decision

### The two halves cost wildly different amounts

**Knowing a language costs a row in a table.** Data, no binary.
**Highlighting one costs a grammar**, and
[ADR 0085](0085-a-second-grammar-for-cpp.md) measured that at 3.3 MB for
C++ alone. Treating them as one decision is what made this look
expensive.

### Every mainstream language is now known

The built-in table covers the languages people run servers for, named
with the **LSP spec's own ids** — a server keys off `python`, never
`py`, and off `javascriptreact` for `.jsx`. With that,
`lang.python.lsp = pylsp` is genuinely all a Python file needs.

An unconfigured `.py` now reads `no lsp` rather than showing nothing,
which is the difference between *"you could configure this"* and *"this
is not code"*. That distinction is the whole point: the blank state was
indistinguishable from success.

### Five grammars, measured before they were chosen

Predicted from the two existing grammars — `parser.c` ÷ 5.5 ≈ library
size (C: 3.87 MB → 621 KB; C++: 17.3 MB → 3378 KB) — then checked:

| language | library | |
|---|---|---|
| python | 516 KB | |
| javascript | 381 KB | `.jsx` comes with it |
| css | 106 KB | |
| lua | 64 KB | |
| html | 35 KB | |
| **total** | **1.08 MB** | predicted 1.26 MB |

Binary 5.45 MB → 6.43 MB, **+18%**. Startup unchanged at 62 ms: the
grammars are linked but only one is instantiated per buffer.

**Vimscript was measured and dropped.** Its `parser.c` is 4.57 MB —
larger than Python's — for an estimated 830 KB, which would have been
40% of the total spend on the least-read file type of the six. It is a
row in the filetype table, so `lang.vim.lsp` works; there are just no
colours.

### Every node name was checked before a query was written

[ADR 0085](0085-a-second-grammar-for-cpp.md)'s rule: `ts_query_new`
rejects the whole query on one unknown name, `syntax_create` returns
NULL, and the editor shows a language with no colours and no error —
indistinguishable from "not supported yet".

So each candidate name was checked against that grammar's own
`node-types.json` before being written down, and a test now asserts all
seven languages compile their query, capture something, and highlight a
known keyword. That test caught its own first version: it expected `int`
to be a C keyword when the query captures it as a **type**.

## Consequences

Five languages have colours; every mainstream language can have a
server from one config line.

**HTML, CSS and JavaScript do not see into each other.** A `<script>`
block in an HTML file stays grey, because that needs Tree-sitter
injections — a real feature, not a query line, and deliberately separate
from this.

Adding the next language is one `FetchContent_Declare`, one name in each
of two `foreach` lists, one enum value, one `case`, one `.scm`, and one
row in the grammar table. No architecture changes; the wiring was already
a loop.

The queries stay deliberately thin — keyword, comment, string, number,
type — because that is all the theme has
([ADR 0048](0048-syntax-accent-colors-caret-inset-status-init.md)). CSS bends it slightly:
property names read as keywords and selectors as types, which is the
mapping that makes a stylesheet legible with only those five.
