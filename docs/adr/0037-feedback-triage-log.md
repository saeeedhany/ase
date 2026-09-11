# ADR 0037: A feedback triage log, alongside ADRs

## Status

Accepted

## Context

The first external review of the alpha release came in as seven
freeform comments on Discord. They needed a real, considered response
each (checked against the actual code, not just acknowledged), and the
user wanted that response — and who raised each point — recorded
formally rather than left to scroll off in a chat channel.

## Decision

A new `docs/feedback/` tree, structured the same way `docs/adr/`
already is: one file per batch (`docs/feedback/NNNN-slug.md`), a
`template.md` excluded from the build via `exclude_docs` (same
mechanism as `adr/template.md`), an `index.md` table of every batch,
and an explicit nav entry per file in `mkdocs.yml` — no new plugin,
consistent with ADR 0035's decision to keep the nav explicit rather
than chase directory auto-discovery.

Each batch file opens with reporter identity (name, GitHub handle) and
source (a link to the originating Discord message, GitHub issue, etc.)
and a date, then one subsection per comment: the comment quoted
verbatim, a status (`Open` / `Fixed` / `Won't Fix` / `Not a Bug`), and
a formal response explaining what was actually checked and why,
linking forward to the resolving commit/PR once one exists.

This complements issue tracking rather than replacing it: Discord,
GitHub issues, or wherever a report first lands stays the live
conversation venue (threading, back-and-forth, notifications); this
log is the curated, versioned record of what was raised and what was
decided, sitting next to the ADRs that record why the project itself
is shaped the way it is. Status is intentionally not "resolved and
forgotten" — an item stays `Open` in the doc itself until the actual
fix lands, so the log can't drift ahead of reality by marking
something done before it is.

Batch 0001 records all seven comments from this first review: two real
packaging bugs (`.deb`/AppImage version floors baked in from being
built on a rolling-release dev machine instead of an old, pinned base
— see the doc for the fix), a real CMake robustness gap (missing Qt6
warns instead of failing when the GUI is explicitly requested), a real
upstream compiler warning (vendored Lua's `tmpnam()` path, fixable via
one missing compile definition), a real but small UI fix (the Help
panel's fixed-size single-column layout forcing an inner scrollbar),
and two items checked and found not to be current bugs (a README
undo/redo inconsistency that no longer exists in the file, and
"unfinished-looking" window buttons that turned out to be the
reporter's own window manager chrome, not anything ase draws).

## Consequences

Future external feedback — from any source — gets the same treatment:
a new numbered batch file, checked against the code, answered formally,
and left `Open` until actually fixed. The five real, open items from
batch 0001 are tracked here rather than as an implicit to-do list, and
each will get its own commit that flips its status once done.
