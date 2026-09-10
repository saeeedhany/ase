# ADR 0033: License chosen (Apache 2.0) — first alpha release

## Status

Accepted. Supersedes [ADR 0003](0003-license-decision-pending.md).

## Context

ADR 0003 deliberately left the license undecided (spec section 9's
permissive-vs-copyleft call) and, as an explicit consequence, barred
pushing the repository to a public remote or accepting external
contributions until it was resolved. That gate is now being lifted:
the user asked to publish the project's first alpha release.

The requirement given: stay open source, but anyone who modifies or
redistributes the code must credit the author.

## Decision

**Apache License 2.0.** Permissive (no requirement that derivative
works or plugins be released under the same license — unlike GPL/LGPL,
the other side of ADR 0003's original fork), and the closest standard
match to the stated requirement: redistribution must retain copyright/
attribution notices (§4(c)), and — the part MIT/BSD don't have —
**modified files must carry a prominent notice stating they were
changed** (§4(b)). It also adds an explicit patent grant (§3) neither
MIT nor BSD provides, relevant given this project has a plugin ABI
(ADR 0009) third parties can build against.

`LICENSE` (full Apache 2.0 text) and `NOTICE` (the copyright line:
"Copyright 2026 Saeed") added at the repository root — the standard
two-file layout the license's own Appendix recommends. `docs/adr/0003`
is left in place, not deleted, with its Status line updated to point
here — ADRs are a decision log, not something rewritten after the
fact.

### What changed alongside it

- `README.md`: dropped the "not published publicly, license still
  open" framing; License section now points at `LICENSE`; Contributing
  section no longer says external contributions aren't accepted;
  Status section rewritten to reflect Phases 8 through 17.5 actually
  being done (it had been stale since roughly Phase 7).
- `CONTRIBUTING.md`: dropped the "not accepting contributions yet"
  status line.
- `CMakeLists.txt`: `project(... VERSION 0.0.0)` → `0.1.0` — CMake's
  `VERSION` field is strictly numeric, so the "-alpha" release-stage
  label is appended separately, at the GUI target, as
  `ASE_VERSION_STRING` (`gui/CMakeLists.txt`,
  `target_compile_definitions`) — the one place that string is now
  defined, read by `AboutPanel` instead of a second hardcoded literal.
- `AboutPanel`: version string, license line, and a third link (this
  project's Discord, alongside the existing GitHub/website links) —
  see docs/adr/0022 for why this panel exists and its general shape.
- A local git tag, `v0.1.0-alpha`, marks this point.

## Consequences

The repository is now clear to push to a public remote and to accept
external contributions whenever the user chooses to — that specific
step (creating/pushing to an actual GitHub remote) was deliberately
scoped out of this change at the user's own direction; this ADR only
covers the local tag and the in-repo license/version/about-panel
changes. Full test suite unaffected (docs/build-metadata only, no
core or GUI logic changed besides the About panel's template string
and version macro) — 9/9 still passing.
