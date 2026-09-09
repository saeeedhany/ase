# ADR 0002: Core engine builds and runs headless

## Status

Accepted

## Context

The spec (section 3) mandates that the core library must build and run
without any GUI dependency, to force real core/GUI separation and to keep
future frontends (TUI, embeddable widget) possible without rewriting the
engine. It's easy to let this erode by accident — a single `#include
<QtCore/...>` or a callback signature shaped around Qt types is enough to
couple the engine to the toolkit.

## Decision

- `core/` is a standalone C library (see ADR 0004 for the C-vs-C++ choice)
  with a stable, plain C header ABI. It has zero build or link dependency
  on Qt, and zero dependency on `gui/`.
- `gui/` depends on `core/`; `core/` never depends on `gui/`.
- The root `CMakeLists.txt` always configures and builds `core/` and its
  tests; the `gui/` subdirectory is only added when `ASE_BUILD_GUI` is on
  and Qt6 is found, so a headless build is always possible.
- CI must include at least one job that builds and tests `core/` alone
  (`ASE_BUILD_GUI=OFF`), so this boundary is enforced by the build rather
  than by convention.

## Consequences

Contributors touching the engine can iterate without Qt installed. Adding a
TUI or an embeddable widget frontend later means writing a new thin shell
against the existing C ABI, not restructuring the engine. The cost is
discipline: engine code can never reach for a Qt convenience type, even
when it would be locally simpler.
