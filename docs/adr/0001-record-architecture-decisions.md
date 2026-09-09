# ADR 0001: Record architecture decisions with ADRs

## Status

Accepted

## Context

The project spec (section 8) requires that major structural choices be
documented with *why*, not just *what*, from the first commit — so future
contributors can extend the system without reading the whole codebase or
re-litigating settled decisions.

## Decision

We record architecturally significant decisions as Architecture Decision
Records under `docs/adr/`, numbered sequentially, using the template in
`docs/adr/template.md`. A decision is ADR-worthy if reversing it later would
require touching multiple modules or would be expensive/risky — not every
implementation detail needs one.

## Consequences

Decision history is greppable and versioned alongside the code it affects.
Superseding a decision means adding a new ADR that references the old one,
not editing history.
