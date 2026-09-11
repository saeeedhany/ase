# ADR 0038: Feedback triage criteria, and a "Deferred" status

## Status

Accepted

## Context

Working through batch 0001 surfaced a real gap in ADR 0037's status
list (`Open` / `Fixed` / `Won't Fix` / `Not a Bug`): item 5 (the Help
panel's cramped, single-column shortcut list) is a real, verified
observation — a two-column layout was actually built and confirmed to
remove the inner scrollbar entirely — but the fix was reverted because
redesigning one panel's layout in isolation would fight the shared
visual language every floating panel currently uses
([ADR 0022](0022-floating-panel-design-system.md)). None of the four
existing statuses fit: it isn't `Open` (that implies "just go fix it
as reported"), it isn't `Won't Fix` (the concern is valid, not
rejected), and it certainly isn't `Not a Bug` (the reporter is right
that the panel is cramped).

Separately, the user asked for an explicit answer to a broader
question: what stops a future report from mislabeling a deliberate
design choice as a defect, and how should that distinction get made
consistently rather than argued fresh each time?

## Decision

### A fifth status: `Deferred`

For a report that's accurate and worth acting on, but where the right
fix is part of a future, coordinated design pass across the whole
system rather than a one-off patch to the single thing reported.
Distinct from `Won't Fix`: the concern is tracked and expected to be
revisited, not rejected. `docs/feedback/template.md` and
`docs/feedback/index.md` both updated with the full five-status list.

### Explicit triage criteria, in `docs/feedback/index.md`

A comment being *true* doesn't automatically make it a *bug*. ase has
explicit, documented design pillars
([`docs/SPEC.md`](../SPEC.md#2-design-pillars-non-negotiable)) and a
shared visual language across floating panels (ADR 0022). A report is
checked against what ase is actually trying to be, not against some
other editor's convention, before it's treated as broken:

- Always actionable regardless of taste: a crash, incorrect output, a
  build failure, a broken documented promise ("download this and it
  runs"), or a concrete doc/code contradiction.
- Evaluated against the project's own design intent first: subjective
  "looks unfinished" / "feels cramped" comparisons against some other
  editor's convention. Real gaps found this way land on `Deferred`,
  not `Open` — see above.

This is written down publicly (`docs/feedback/index.md`, linked from
every batch) rather than decided ad hoc per report, so a reporter can
see the reasoning up front and a maintainer has a consistent standard
to apply rather than relitigating it each time.

## Consequences

Batch 0001's item 5 is now `Deferred`, not `Open` — accurately
reflecting that the fix was found, verified, and intentionally not
merged, rather than "not yet looked at." Future batches use the same
five statuses and the same written criteria. The tradeoff: `Deferred`
items need their own follow-up mechanism eventually (the "system-wide
design pass" they're deferred to isn't itself tracked as a task yet) —
accepted for now, revisited once there are enough deferred items to
justify one.
