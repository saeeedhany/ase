# ADR 0121: Saying where the keyboard is

## Status

Accepted

## Context

ADR 0120 gave regions a vocabulary. It did not give them a *look*: with
the panel focused, the editor's caret went on pulsing exactly as if it
were about to receive the next keystroke, and nothing on screen said
otherwise.

Two smaller things came with it. Walking a list of references took the
keyboard away after every jump, and a half-typed Vim command was
invisible until it resolved.

## Decision

### The caret stops when it is not listening

Unfocused, the caret drops to alpha 70 and stops animating. A caret that
pulses is claiming the next keystroke; when the panel has focus that is a
lie, and it is the most prominent thing on the screen telling it.

Dimmed rather than hidden, because it is still where you were and where
`Ctrl+W k` will put you back.

### The seam says which side is live

It sits at 22% normally and 60% when the panel holds the keyboard —
still under the 100% it reaches on hover, so the three states stay
distinguishable.

The divider is the one piece of chrome both regions share, which makes
it the natural place to say which one is active without adding anything
new to the screen. Measured: 45 against 123 in row luminance.

### Enter keeps the keyboard in the list

Walking references is a sequence, not a jump. Taking focus to the editor
after each one meant reaching back for it every time, which is what made
the panel feel like something you visit rather than something you work
in. `Ctrl+W k` leaves when you have arrived somewhere worth staying.

`openBuffer()` focuses a viewport on its way, so this takes focus back
rather than merely not giving it away.

### The half-typed command is shown

Vim's `showcmd`, immediately left of the position readout, where vim
puts it. `2d` sits there until the motion arrives. Without it a pending
operator is invisible and the keystroke that completes it appears to do
something arbitrary — which is exactly the confusion a modal editor owes
its user an answer to.

It clears when the command resolves or is abandoned, and it belongs to
the buffer it was typed in, so switching buffers clears it.

## Consequences

The three uses of the seam's brightness — resting, active region, hover
— are close enough to need checking rather than assuming. 22%, 60% and
100% read as distinct in practice; a fourth state would not.

`focusInEvent` resets the caret's animation clock, so returning to the
editor starts from full brightness rather than from wherever the fade
had got to when focus left.
