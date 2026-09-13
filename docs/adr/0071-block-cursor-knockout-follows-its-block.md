# ADR 0071: The block cursor's knockout follows its block

## Status

Accepted

## Context

Reported from use: the character under the block cursor is not fully
visible while the cursor is in the faded part of its blink.

## Decision

### What was actually wrong

The guess in the report was that the character's opacity had been
lowered. It was the opposite: the character was being *erased* by
something that was not fading at all.

The block cursor is drawn in three steps. `drawLine()` paints the real
text first. The block is then filled over it in the text colour at the
blink's current alpha. Finally the glyph is drawn again on top in the
**background** colour, terminal style, so it reads as inverse video
against an opaque block.

That last step was unconditional and fully opaque. So at the dim end of
the blink the block had faded to nearly nothing while a
background-coloured glyph was still being painted at full strength over
the real text — background on background. Measured on `mmmm mmmm`: the
character's contrast against its own cell fell to **21/255** at the
trough, against 181 at the peak, while the identical characters beside
it stayed at full strength.

### The fix, and why it is a threshold rather than a fade

The knockout now only happens while the block is at least half opaque.

The tempting fix — fade the knockout along with the block — does not
work, and the reason is worth writing down. With the block at alpha `a`
and the knockout at `k`, the glyph ends up at `mix(T, bg, k)` and the
cell behind it at `mix(bg, T, a)`, so the contrast between them is

```
|T - bg| · |1 - k - a|
```

That vanishes whenever `k + a == 1`. Any continuous knockout ramp from
0 to 1 crosses that line *somewhere*: `k = a` simply moves the invisible
moment from the end of the blink to its middle. There is no continuous
choice that avoids it.

A step keeps `|1 - k - a|` at 1/2 or better everywhere — contrast is
`1 - a` below the threshold and `a` above it, both at least a half. It
also puts the single switch at the midpoint of a cosine, which is where
the curve moves fastest and therefore dwells least.

## Consequences

Measured over 14 consecutive frames of the blink, same file and cell:

| | worst contrast |
|---|---|
| before | 21 / 255 |
| after | **52 / 255** |

Visually: at the trough the character under the cursor is now
indistinguishable from its neighbours — full colour, fully legible. At
the peak it is still knocked out in the background colour against a
solid block. At the crossover it is a lighter glyph on a half-strength
block, and readable.

This supersedes the claim in ADR 0047's code comment that the knockout
"guarantees legibility regardless of the block's opacity". It did the
opposite for half of every blink, and had been doing so since the block
cursor shipped.

`ctest` 9/9, clean build, zero warnings.
