# ADR 0003: License is deliberately undecided

## Status

Proposed (open — blocks making the repository public)

## Context

Spec section 9 calls for a deliberate choice between a permissive license
(MIT/Apache-2.0) and a copyleft license (GPL/LGPL), based on the desired
plugin ecosystem dynamics: permissive licensing lowers friction for
third-party and commercial plugins/forks; copyleft (especially LGPL for the
core, or GPL) can force derivative works and plugins to stay open, which
matters more once the plugin ABI (Phase 5) exists.

This is explicitly called out as a decision to make deliberately, not by
default — so no license has been chosen or assumed on the project's behalf.

## Decision

No `LICENSE` file is added yet. Until this ADR is superseded with an actual
choice:

- The repository must not be pushed to a public remote or otherwise
  distributed — with no LICENSE file, default copyright law applies and no
  one else has rights to use, modify, or redistribute the code.
- External contributions should not be accepted, since contributors would
  have nothing to license their contribution under.

## Consequences

This decision is revisited before Phase 5 (plugin ABI) at the latest, since
the license directly shapes what plugin authors are allowed to do. Revisit
sooner if the repo is meant to go public earlier than that.
