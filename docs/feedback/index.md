# Feedback

External review comments — from testers, Discord, GitHub issues, or
anywhere else — recorded one batch per source/date, with a formal
response to each item. This complements [issue tracking](https://github.com/saeeedhany/ase/issues)
rather than replacing it: an issue or Discord thread is where a
conversation happens live, this is the curated record of what was
raised and what we decided.

| # | Batch | Reporter | Source |
|---|-------|----------|--------|
| [0001](0001-alpha-release-external-review.md) | Alpha release external review | Ajeep ([`@TOTO-sys28`](https://github.com/TOTO-sys28)) | [Discord](https://discord.com/channels/1547730271695282258/1547848976835936266) |

## How we triage feedback

Every item gets checked against the actual code before it gets a
status, never just acknowledged:

- **Open** — a real, confirmed defect, not yet fixed.
- **Fixed** — a real, confirmed defect, resolved; the response says
  what changed and how it was verified.
- **Not a Bug** — checked against the code and it isn't a defect:
  either already correct, or describing behavior the reporter's
  environment produced (window manager theming, a stale local build)
  rather than anything ase's own code does.
- **Deferred** — a real, valid observation about something the editor
  could do better, but the right fix is part of a broader, coordinated
  design pass, not a one-off patch to the single thing that got
  reported. Distinct from "Won't Fix": the concern stands and is
  tracked, just not actioned in isolation.
- **Won't Fix** — a deliberate design decision, kept as-is on purpose.

**A comment being true doesn't automatically make it a bug.** ase has
a small number of explicit, documented design pillars and a shared
visual language across its floating panels — see
[`docs/SPEC.md`](../SPEC.md#2-design-pillars-non-negotiable),
[ADR 0007](../adr/0007-syntax-highlighting-tree-sitter.md) (monochrome
by design), and [ADR 0022](../adr/0022-floating-panel-design-system.md)
(the shared panel system). A report that a specific screen "looks
unfinished" or "feels cramped" compared to some other editor's
convention is useful input, but it's evaluated against *what ase is
actually trying to be* before it's treated as something broken. Where
a comment does point at a real gap in that identity, the fix belongs
in a real, coordinated design pass across the whole system — not a
sequence of isolated patches that would leave panels looking like they
were designed by different people at different times. That's what
**Deferred** is for, and it's a genuine "yes, and" rather than a
polite way of saying no.

What always counts as an actionable bug regardless of taste: a crash,
incorrect output, a build failure, a broken documented promise (e.g.
"download this package and it runs"), or a concrete contradiction
between what a doc says and what the code does.
