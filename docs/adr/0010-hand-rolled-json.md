# ADR 0010: Hand-rolled JSON, not a vendored library

## Status

Accepted

## Context

The LSP client (ADR 0011) needs to speak JSON-RPC, which means parsing
and writing JSON. A small, well-known C library (e.g. cJSON) would work
and is genuinely tiny — this isn't the same "avoid a whole grammar/spec"
argument ADR 0008 made against TOML. The question here is narrower:
given the project already takes on Tree-sitter and Lua as `FetchContent`
dependencies, is a third one for JSON worth it?

## Decision

No — `core/src/json.c` / `core/include/ase/json.h` is a hand-rolled,
deliberately minimal JSON value tree: parser, writer, and a small
builder API (object/array/string/number/bool/null), scoped to exactly
what LSP messages need. Reasoning, following ADR 0008's precedent:

- **Full control over the exact subset used.** The LSP client only ever
  needs to build flat-ish request/notification objects and read a
  handful of well-known response shapes. A general-purpose library
  brings correctness/performance concerns (duplicate-key handling,
  number precision edge cases, big-file streaming) this project will
  never exercise.
- **One less external revision to track.** Tree-sitter and Lua are both
  pinned to upstream tags/commits already (ADR 0007, ADR 0009); every
  additional `FetchContent` dependency is one more thing to bump,
  re-vet, and potentially hit ABI/build surprises with (both prior
  dependencies already did, in different ways).
- **Correctness is directly testable and owned.** `core/tests/test_json.c`
  and `core/fuzz/fuzz_json.c` exercise exactly this implementation —
  nested structures, string escaping, UTF-16 surrogate pairs (real LSP
  traffic carries arbitrary source-file text through JSON strings, so
  this has to be right, not just "usually right"), malformed-input
  rejection, and builder/writer round-trips. 37.8M fuzz executions
  clean under ASan/UBSan with no crashes (low code-coverage plateau —
  expected for a strict-grammar format under unguided byte mutation,
  not a sign of a shallow test).

## Consequences

The JSON module is intentionally not a general-purpose library:
duplicate object keys silently keep only the last value (fine for
LSP's own well-formed messages), and there's no streaming/incremental
parse API (every message is small enough to parse whole). If a future
feature needs genuinely general JSON handling, that's a signal to
revisit — not something this module tries to anticipate now.
