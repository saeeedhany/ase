# ADR 0124: The debounce that never happened

## Status

Accepted

## Context

ADR 0107 decided that past 256KB the syntax parse should wait for a
pause in typing rather than run on the keystroke, so a large file types
plainly instead of freezing. Measuring a keystroke by file size showed
it had never worked:

| size | `.c` (a grammar) | `.txt` (none) |
| --- | --- | --- |
| 13KB | 800us | 8us |
| 56KB | 1309us | 15us |
| 144KB | 2503us | 31us |
| 290KB | 4008us | 55us |
| 603KB | 8294us | 108us |

The last two rows are above the threshold, where the grammar was
supposed to cost nothing until typing stopped. It cost 73 times the
same file without one.

## Decision

`refreshCache()` runs on every keystroke and zeroes the capture window,
because the byte offsets it holds no longer mean anything once the text
has changed. Over the threshold it then armed the timer and returned,
which looked correct.

But `ensureCursorVisible()` runs later in the same keystroke and needs
captures to measure the caret: a keyword is bold, a comment italic, so
the x of a column depends on them. It asks for its line with
`ensureCaptureWindow(..., force=false)`, whose early return compares
against the window that was just zeroed — so it never returned early,
and re-parsed the whole file. Every keystroke, at every size.

The debounce was not defeated by a subtle race. It was defeated by the
line four statements above it.

While a parse is pending the window now stays empty and every unforced
caller is turned away. The timer's call is the only forced one, so it
still fills the window when typing stops.

## Consequences

| size | before | after |
| --- | --- | --- |
| 290KB | 4008us | 56us |
| 603KB | 8294us | 115us |

At both sizes a file with a grammar now costs what the same file
without one costs, which is the whole claim ADR 0107 made.

Measuring the caret against an empty window means measuring it as plain
text. That is the right answer rather than a concession: the text on
screen is *also* drawn plain while the parse is pending, so the caret
now agrees with what is under it. Forcing the parse was what made them
disagree.

Below the threshold nothing changes. A keystroke there is
`ts_parser_parse_string` almost entirely — 0.54ms at 13KB, 2.13ms at
144KB, against a flat 0.26ms for the windowed query — which is
tree-sitter walking a tree that size, and under a frame.

`typingCostByFileSize` in `gui/tests/test_keybindings.cpp` now asserts
the two columns stay within a small factor of each other above the
threshold. It fails on the old code with the numbers above.
