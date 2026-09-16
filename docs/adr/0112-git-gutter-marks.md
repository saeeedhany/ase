# ADR 0112: Git gutter marks

## Status

Accepted

## Context

The roadmap has listed gutter marks as "cheap next to the existing
diagnostic-gutter machinery, and disproportionately useful". The gutter
already paints a severity dot per line, so there was a place to put them.

## Decision

### Only the hunk headers, from `git diff -U0`

`core/src/vcs.c` reads the `@@ -a,b +c,d @@` lines and nothing else.
They are the only part of git's output whose format is documented as
stable, and the only part needed. `-U0` keeps every hunk exact rather
than padded with three lines of context.

The shapes were confirmed against real git rather than recalled, one
case each:

| edit | header |
|---|---|
| insert 1 line after line 2 | `@@ -2,0 +3 @@` |
| insert 3 lines after line 2 | `@@ -2,0 +3,3 @@` |
| delete lines 3–4 | `@@ -3,2 +2,0 @@` |
| change line 2 in place | `@@ -2 +2 @@` |
| replace 2 lines with 4 | `@@ -2,2 +2,4 @@` |
| delete the first line | `@@ -1 +0,0 @@` |
| insert at the very top | `@@ -0,0 +1 @@` |

An omitted count means 1. A `+N,0` deletion has no line of its own, so
it is marked against line N — the last line above the gap, or the first
line when the file's opening lines were what went.

### git is only spawned inside a repository

Outside one, `git diff -- <path>` does not fail cleanly. It falls back
to `--no-index` mode, warns, and prints seven kilobytes of usage text
with exit 129 — on every open and every save, for every file that is not
in a repository.

Worse, spawning it there made the editor intermittently receive a
`SIGTERM`. Measured rather than guessed: 0 of 10 runs on the commit
before this work, 3 of 10 with the git spawn in place, 0 of 10 once the
spawn was gated. The precise mechanism was not isolated — the signal's
sender pid was the editor's own, and neither of the two `kill()` sites
in `process.c` can pass a pid that would explain it. What is established
is the trigger and that removing it removes the symptom.

So a `.git` entry at or above the file is checked first, and git is
never run where it has nothing to say.

Not with `project::rootFor()`, which was the first attempt and was
silently useless: it answers "where does this project start", and when
it finds no `.git` it returns the directory it was handed rather than
nothing. Used as a yes/no test it passed every file, and the measurement
still showed 3 of 8 — which is what said the gate was not working.

### git is spawned and polled, never waited on

The same shape as `:compile` (ADR 0029). A cold repository or a network
filesystem can make git slow, and a blocked paint is worse than a late
mark. A non-zero exit — not a repository, file not tracked, no git on
PATH — means no marks and no complaint, because a file outside git is
the ordinary case and not an error.

### The marks hide when an unsaved edit has moved the lines

git diffs the file **on disk**, so the marks describe the saved version.
Typing inside a line moves nothing and the marks stay accurate. Adding
or removing a line shifts every line below it, and the marks would then
point at the wrong rows — so they are dropped until the next save, when
git runs again.

A mark in the wrong place is worse than no mark. Doing better would mean
diffing the buffer rather than the file, which means owning a diff
algorithm rather than borrowing git's, and that is a much larger thing
than this.

### A 2px bar at the very edge

Left of the diagnostic dot and the line numbers. Green for added, amber
for changed, red for removed — the only place in this editor that spends
colour on something other than syntax, because added and removed have no
other vocabulary. Alpha 190, because the text is the focus.

`git_marks = false` turns it off.

## Consequences

Twelve parser tests, cross-checked by running the editor's parser over
real `git diff` output for each shape and comparing the marks it
produces against the file's actual lines.

Seven mutations, five caught immediately. The two that escaped were both
worth the trouble:

- No test asserted that a header **without its closing `@@`** produces
  nothing, only that it did not crash. Tightening that assertion found a
  real bug: `@@ -1,1 +0,99999999 @@` was accepted and marked 99,999,998
  lines from a negative offset. Line 0 exists only as "before the first
  line", which is what a deletion means, so a non-empty range starting
  there is now rejected.
- The other exposed dead code. `ase_vcs_diff_status` preferred a
  modification over a deletion where both covered a line — but git
  merges adjacent changes into a single hunk, so a line changed next to
  lines removed comes back as one `@@ -1,3 +1 @@`, never as two
  overlapping hunks. Verified against git, then deleted; the ranges
  cannot overlap and there was nothing to prefer between.

Marks refresh on open and on save. An external `git commit` or
`git checkout` while the file is open leaves them stale until the next
save, since nothing watches the repository.

A `.git` **file** counts, not just a directory: that is what a worktree
or a submodule has, and testing for the type rather than for existence
would have excluded both.
