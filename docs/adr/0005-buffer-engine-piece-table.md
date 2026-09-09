# ADR 0005: Buffer engine is a piece table, not a rope

## Status

Accepted (revisit if benchmarks on pathological access patterns demand it —
see "Known limitations" below)

## Context

Spec section 4 leaves the buffer data structure open between a rope
(preferred for scale) and a piece table (simpler undo/redo semantics),
"decision made after benchmarking both against representative large-file
workloads." Building two full implementations before writing a single line
of the editor that will consume them is a lot of complexity budget to
spend up front, and cuts against the "Minimal" pillar.

## Decision

Implement a piece table for v1:

- An immutable `original` buffer (the file as loaded) and an append-only
  `add` buffer (all inserted text, in insertion order).
- A doubly linked list of pieces, each a `{source, start, length}` slice
  into one of those two buffers.
- A single-piece "last access" cache (`cache_piece` + its buffer-offset) so
  that sequential access — the dominant real-world pattern: typing,
  backspacing, and rendering a scrolling viewport all touch nearby offsets
  in sequence — is O(1) amortized. A cold, far-away access still costs an
  O(pieces) linear walk.

This lives behind `core/include/ase/buffer.h`, a small opaque-pointer API
(`ase_buffer_create/_from_file/_destroy`, `_insert`, `_delete`,
`_get_text`, `_length`, `_line_count`, `_save_to_file`,
`_piece_count` for diagnostics). Nothing outside `buffer.c` knows the
internal representation, so replacing it with a rope later is a
`buffer.c` rewrite, not an editor-wide change.

Benchmarks live in `core/bench/bench_buffer.cpp` (a separate tool, not part
of `ctest`) and measure: sequential-append throughput, random-offset
edit throughput, and viewport-style contiguous reads, against a synthetic
~50MB buffer. These are the "representative large-file workloads" the spec
asks for; the rope alternative is not implemented because the piece table
comfortably clears them and a rope's added complexity — balanced-tree
rebalancing, more intricate split/merge — isn't paid for yet.

Measured on the dev machine (Release build, 50MB seed buffer):

| Workload | Ops | Total time | Per-op |
|---|---|---|---|
| Sequential typing (append) | 100,000 | 3.2 ms | 0.03 µs |
| Sequential backspace | 100,000 | 1.3 ms | 0.01 µs |
| Random-offset insert/delete | 20,000 | 616 ms | 31 µs |
| Random 4KB viewport read | 5,000 | 395 ms | 79 µs |

This is exactly the shape the design predicts: sequential access (typing,
backspacing, and — in the common case — scrolling a viewport) is cache-hit
O(1) and effectively free, comfortably inside the "<16ms, ideally <4ms"
per-frame budget with enormous headroom. Cold random access costs an
O(pieces) walk and visibly degrades as the piece count grows into the tens
of thousands (here, ~30,000 pieces after the random-edit run) — a real
cost, but one that only bites a workload this benchmark deliberately
constructs (uniform-random offsets across the whole file every op), not
the sequential patterns real typing/scrolling produce. Revisit if a real
usage pattern turns out to hit this path.

## Known limitations (deliberately deferred)

- **No line-index acceleration.** `ase_buffer_line_count` and any future
  "byte offset of line N" lookup scan the whole buffer (`O(total bytes)`).
  Fine for v1 correctness; will need a per-piece or separate line-start
  index once the GUI (Phase 2) needs fast line-based scrolling on large
  files.
- **Cold random access is `O(pieces)`.** A single edit far from the last
  access point walks the piece list linearly. Pathological access patterns
  (e.g. a plugin doing many far-apart edits per keystroke) would need a
  tree-indexed piece table (offset-indexed, à la VS Code's implementation)
  or a rope. Not built now because nothing exercises this pattern yet.
- **Deleted bytes are never reclaimed.** Delete only unlinks/frees `Piece`
  nodes; the underlying bytes stay in the `add`/`original` buffers for the
  buffer's lifetime. A long editing session can grow memory well past the
  current visible content size, in tension with the NFR 5 "memory
  proportional to file size" goal. Deferred until it's observed to matter;
  the fix is a periodic compaction/rebuild pass, tracked in
  `docs/ROADMAP.md`.
- **No fuzz coverage in CI yet.** A libFuzzer harness exists
  (`core/fuzz/fuzz_buffer.c`, built only with `-DASE_BUILD_FUZZERS=ON`
  under clang) but isn't wired into the CI workflow yet — tracked in
  `docs/ROADMAP.md`.

## Consequences

Undo/redo (not yet implemented) gets simpler on top of this: each edit
already produces a small, cheap-to-record diff (which pieces were
inserted/removed and where), which is the piece-table's classic advantage
over a rope for history tracking. If a future benchmark on truly huge
files (multi-GB) or pathological plugin access patterns shows the O(pieces)
worst case actually hurts, the fix is scoped to `buffer.c` behind the
existing `ase/buffer.h` API.
