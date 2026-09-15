# ADR 0104: The vim conformance suite

## Status

Accepted

## Context

The vim layer is 2,347 lines in one file — the largest and most
intricate part of the editor — and had **no automated coverage at all**.
The nine existing suites are all in `core/` and `modules/`;
`gui/tests/` did not exist.

Its behaviour had been verified, repeatedly and carefully, by running
the same keys through real vim and diffing the bytes. Roughly 150 such
comparisons were made across the work that produced
[ADR 0097](0097-marks-and-macros.md) through
[ADR 0102](0102-substitute.md). **Every one of them was a throwaway
script in a scratch directory.** None survived the session that made it.

The evidence that this mattered is in the bug pattern. The phantom
`m_lineStarts` entry past a trailing newline caused three separate bugs
(`G`, `vGd`, `J`). The guard in `vimApplyNormalKey` that abandons a
pending operator caught three separate features (marks, `d'a`, text
objects). The same defect kept resurfacing because nothing pinned the
behaviour down between sessions.

## Decision

### In process, not through the GUI driver

The driver that has been used for verification lives in `.claude/` and
is deliberately untracked, so a committed test cannot use it. That
constraint turned out to be a favour: the suite constructs a real
`EditorViewport`, sends `QKeyEvent`s to it, and reads the buffer back.
No VNC, no screenshots, no sleeps. **91 cases in 0.03 seconds**, against
several seconds *per case* through the driver.

`gui/` now builds an `ase_gui_objects` object library that both the
executable and the test link, so the test drives the same widgets the
user does rather than a second copy of the sources. `main.cpp` stays
with the executable, since it carries `main()`.

### Expectations are generated from vim, and committed

`derive_cases.py` runs each case through
`vim -u NONE -i NONE -N -es` with `nofixeol` and writes `vim_cases.inc`.
That file is committed, so **the suite needs no vim to run** — but the
expectations are not one person's memory of what vim does, and anyone
can regenerate them to audit or extend.

Both vim flags are load-bearing and both produced a wrong answer during
this work: without `-i NONE`, viminfo carries registers between runs and
`p` appears to paste something never yanked; without `nofixeol`, vim
appends a trailing newline and two identical results look different.

### Four cases vim cannot be scripted into producing

`vim -es` **refuses to record macros inside `:normal`** — after
`qaxjq` the register comes back empty, so `@a` does nothing and the
derived expectation is silently wrong. The first full run "failed" three
macro cases where the editor was right and the generator was not.

Those four live in a `MANUAL_CASES` list with the reason written next to
each, rather than being quietly dropped. The fourth is a file with no
trailing newline, where our buffer terminates the last line and vim's
does not — visible only under `nofixeol`, since vim's own default writes
the same bytes we do.

## Consequences

**The suite found a bug on its first full run.** `j` on the last line
moved onto the position past a trailing newline, where vim stays put —
the *fourth* bug from that same phantom entry. It is fixed the way `h`
and `l` were: the vim path clamps to `vimLastLine()`, the arrow keys are
left alone, because that position is a real navigable line in the
non-vim editing profile and is not one in vim.

91 cases now cover deletes and counts, `D`/`C` asymmetry, word motions
under operators, the charwise-to-linewise promotion, `h`/`l` clamping,
yank and paste, undo, open and replace, `J` and `gJ` spacing, every text
object, marks as motions and as operator targets, macros, and `.` repeat.

Deliberately not covered yet: `:` commands including `:s`, which go
through `runCommand` rather than key events and want their own data
table; and anything needing a panel — completion, hover, the find bar —
since those are set on the viewport by the window.

Verified that the suite fails when it should: reverting the `j` fix
turns it red on the exact case, with the differing bytes printed.
