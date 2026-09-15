# ADR 0099: Four vim rules the editor was missing

## Status

Accepted

## Context

Two behaviours had been known-wrong and documented for a while: `2D`
left a blank line vim does not, and `vGd` deleted more than vim. Fixing
them turned up two more, and the four together are worth recording
because each is a rule vim has that is not obvious from the outside —
the kind of thing found by running vim, not by reading about it.

Every case below was verified by running the same keys through
`vim -u NONE -i NONE -N -es` with `nofixeol`, and diffing the written
bytes. Both flags matter: without `-i NONE`, viminfo carries registers
between runs and `p` appears to paste something that was never yanked;
without `nofixeol`, vim silently appends a trailing newline on write and
two correct results look different.

## Decision

### `G` stopped counting the position past a trailing newline

A buffer ending in `\n` gets a final `m_lineStarts` entry at EOF. It is
a valid cursor position but not a line, and `G` used
`m_lineStarts.size() - 1`, so it landed one line below the text. On a
3-line file the status bar read `Ln 4`.

`vimLastLine()` skips that entry. This was the root of the `vGd` bug —
the selection ran to a phantom line and took the rest of the buffer with
it — and of the `Ln 6` readings in earlier testing that nobody had
questioned.

### `D` with a count takes the line, not just its text

vim's rule turns on the cursor's column:

| from | vim |
|---|---|
| `(2,1) 2D` | `aaa` / `ddd` — lines 2 and 3 gone |
| `(2,2) 2D` | `aaa` / `b` / `ddd` — line 2 keeps `b` and its newline |
| `(2,3) 2D` | `aaa` / `bb` / `ddd` |

A counted `D` starting at column 1 empties the line as well as taking
the ones below, so the line itself goes. From any other column it
survives with what was before the cursor. `C` does **not** do this — `2C`
collapses the lines into one to type on — so the two stopped sharing
a range.

### `dw` on a line's last word stops at the line end

vim: *"when using the `w` motion with an operator and the last word moved
over is at the end of a line, the end of that word becomes the end of the
operated text."* `dw` on the last word of a line empties the line rather
than pulling the next one up.

### A charwise delete covering whole lines becomes linewise

The rule that explains the rest. `2dw` down a column of single-word
lines deletes *both lines entirely*, not their text — because vim
promotes a charwise delete spanning more than one line to linewise when
it starts at column 0 and ends at a line end.

Without this, the previous rule produces `aaa` / `` / `ddd` where vim
gives `aaa` / `ddd`. With it, all seven `dw`/`2dw`/`3dw`/`4dw` cases
across two files match.

### Linewise paste keeps its trailing newline

Pasting a yanked line at the end of a file with no trailing newline
prepended a newline and **chopped the trailing one**, so `yyp` on `a\nb`
gave `a\nb\nb` where vim gives `a\nb\nb\n`. The leading newline is
needed; dropping the trailing one was not.

## Consequences

31 of 31 comparisons against vim now match, covering `x`, `dd`, `dj`,
`dk`, `D`, `dw`, `cw`, `yw`, `yy`, `cc`, `o`, `O`, `p`, `P`, `u` and
counted forms, on files with and without a trailing newline.

**Performance is unchanged**, measured against the commit ADR 0095's
baseline was taken from, same method, same machine:

| | baseline | now |
|---|---|---|
| startup 11 / 60 / 277 KB | 62 / 81 / 122 ms | 61 / 81 / 122 ms |
| RSS 11 / 60 / 277 KB | 44.9 / 48.9 / 55.1 MB | 44.9 / 49.0 / 55.3 MB |
| typing, 277 KB with clangd | 6.8 ms | 6.67 ms |

The first 60 KB startup comparison read 81 → 102 ms and was noise: over
nine runs each, the baseline ranged 61–122 ms with a median of 81, and
the current build 81–82. A single median is not a measurement when the
distribution is that wide.

Scroll now measures 17.0 ms rather than ADR 0095's 16.1 ms. That is
[ADR 0091](0091-the-clock-follows-the-display.md), not a regression: the
clock follows the display, and a 60 Hz screen rounds to 17 ms. The old
figure came from a hardcoded 16.

Two things remain different from vim and are features rather than bugs:
`J` (join) and text objects (`ciw`, `di(`) do not exist.
