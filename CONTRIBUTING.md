# Contributing

**Status:** the license decision is still open
([ADR 0003](docs/adr/0003-license-decision-pending.md)), so this repo isn't
accepting external contributions yet. This document exists so the process
is ready once that's resolved.

## Ground rules

- Every architecturally significant change (new module boundary, new
  dependency, a change to the core/GUI contract, a data format decision)
  gets an ADR in `docs/adr/`, using `docs/adr/template.md`. Explain *why*,
  not just *what*.
- The core (`core/`) must keep building and passing its tests with
  `-DASE_BUILD_GUI=OFF` and no Qt installed. See
  [ADR 0002](docs/adr/0002-headless-core-separation.md). If a change makes
  that impossible, it's the wrong change or it belongs in `gui/`.
- No feature lands without earning its complexity budget — see the
  "Minimal" pillar in `docs/SPEC.md`. Prefer an opt-in plugin over adding
  to the core feature set.
- Sanitizers (ASan/UBSan) must stay clean on `core/`:
  `cmake -B build -DASE_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug`.

## Workflow

1. Open an issue describing the problem before writing code for anything
   beyond a trivial fix.
2. Keep PRs scoped to one phase/concern from `docs/ROADMAP.md` where
   possible.
3. Add or update tests for `core/` changes; add a benchmark if the change
   touches the buffer engine's performance characteristics.
4. CI must pass, including the sanitizer job, before merge.

## Plugin authoring guide

Not written yet — the plugin ABI and Lua scripting host don't exist until
Phase 5 (see `docs/ROADMAP.md` and `modules/plugins/README.md`). This
section will cover the C ABI contract and Lua API once that phase lands.
