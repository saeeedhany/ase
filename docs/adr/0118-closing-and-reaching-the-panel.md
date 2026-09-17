# ADR 0118: Closing and reaching the panel

## Status

Accepted

## Context

Three things, reported together after using the panel for real.

`Escape` handed focus from the panel back to the editor and nothing
handed it the other way, so once you started typing the panel was
something you could see and not reach without the mouse.

`:q` closed the buffer even with results on screen, which is the larger
and less recoverable of the two things it could have meant.

And the resize introduced in ADR 0117 lurched after a few presses.

## Decision

### The toggle has three states, not two

`Ctrl+Shift+O`:

| the panel is | the key does |
|---|---|
| hidden | show it, and put the keyboard in it |
| visible, editor has focus | move focus to it |
| visible, it has focus | close it |

The middle row is the one that was missing. Closing something you were
not looking at is the more annoying of the two guesses a two-state
toggle has to make.

### `:q` closes the panel first

With results on screen, "close what I am looking at" means the results.
Closing the buffer under them is bigger and harder to undo, and the
panel is the thing that was just opened.

`:q!` deliberately skips this and still closes the buffer. It is the
escape hatch, and one that had to be pressed twice would be a worse one.

### One resize animation, reused

The first version made a new `QPropertyAnimation` per keypress without
stopping the previous one. Two presses looked fine. More than that and
the older animation was still running when the newer one started, and
its `finished` handler set the panel back to *its* target mid-flight —
which is what "starts to give a weird behaviour" was.

It is now a single animation, stopped before each restart, driving
`setFixedHeight` per frame rather than animating `maximumHeight` and
fixing the height at the end. There is no moment when the minimum and
maximum disagree about what is happening.

## Consequences

The resize was checked by pressing faster than the animation and
sampling the seam's position each frame: `339, 279, 219, 159, 155, 155`
— monotonic, then settled. Before, the same test moved backwards.

That measurement was wrong the first time and said the seam was jumping
around. It looked for the brightest row, and a selected row in the
results list is brighter than a 22% hairline, so it had been tracking
the selection. Finding a *thin* bright line — much darker four pixels
above and below — is what the seam actually is.

`:q` was verified by checking the process was still running afterwards,
not by looking at a screenshot of a window that had not closed yet.
