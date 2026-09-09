# Roadmap

Tracks progress against the build order defined in [SPEC.md](SPEC.md#7-build-order--phases).
Each phase should land with tests/benchmarks before the next begins.

## Phases

- [x] **Phase 1 — Core buffer engine**
      Piece-table implementation (`core/src/buffer.c`), unit tests
      (`core/tests/test_buffer.c`), a benchmark tool
      (`core/bench/bench_buffer.cpp`, build with `-DASE_BUILD_BENCH=ON`),
      and a libFuzzer harness (`core/fuzz/fuzz_buffer.c`, build with
      `-DASE_BUILD_FUZZERS=ON` under clang — ~8.8M execs clean under
      ASan/UBSan locally, not yet wired into CI). No GUI dependency; see
      [ADR 0002](adr/0002-headless-core-separation.md) and
      [ADR 0005](adr/0005-buffer-engine-piece-table.md).
      Deferred out of this phase, tracked as follow-ups: undo/redo
      history, line-index acceleration, periodic memory
      compaction, and CI fuzz integration (all called out in ADR 0005).
- [ ] **Phase 2 — Minimal Qt shell**
      Window, custom-painted viewport rendering the buffer, keyboard input
      wired to core edit operations.
- [ ] **Phase 3 — Syntax highlighting**
      Tree-sitter integration into the viewport render path.
- [ ] **Phase 4 — Theming & config system**
      Parser, hot-reload, default theme(s) matching the visual identity.
- [ ] **Phase 5 — Plugin ABI + Lua scripting host**
      Stable extension surface.
- [ ] **Phase 6 — LSP client module**
      Diagnostics, completion, go-to-definition.
- [ ] **Phase 7 — Polish**
      Multi-cursor, minimal/opt-in animations, panel layout, accessibility pass.

## Current status

Phase 1's buffer engine is implemented, tested, benchmarked, and fuzzed
(see above). Phase 2 (Qt shell) has not started.

## Explicit non-goals for v1

- No built-in terminal emulator (defer to external terminal or a later plugin).
- No remote/SSH editing.
- No collaborative/multiplayer editing.
- No plugin marketplace/registry infrastructure — plugins are install-by-file.

## Open decisions blocking later phases

- **License** — permissive vs. copyleft, deliberately deferred.
  See [ADR 0003](adr/0003-license-decision-pending.md) (blocks going public).
- **Config/theme format** — TOML vs. minimal custom format (blocks Phase 4).
- **Exact accent text color** — approx. `#F5E6C8`, to be tuned by eye against
  background `#282828` (blocks Phase 4 default theme).
