# ADR 0082: A bare modifier must not clear a pending count

## Status

Accepted

## Context

`10G` went to the last line instead of line ten. So did `3D`, `2C`,
`5X` — every counted command whose letter needs Shift.

The count was being typed correctly and then thrown away before the
letter arrived. Traced by printing the key, its text and `m_vimCount1`
on every `keyPressEvent`:

```
key=0x31 ('1')        text=49   count1=0    -> 1
key=0x30 ('0')        text=48   count1=1    -> 10
key=0x1000020 (Shift) text=0    count1=10
key=0x47 ('G')        mods=Shift count1=0   <- gone
```

## Decision

### Reject the key on its character, not on emptiness

A bare `Shift` press arrives with an `event->text()` of one NUL
character. That is *not* empty, so it passed the `text.isEmpty()` guard
at the top of the Vim dispatcher, matched no case anywhere below, and
fell through to the `resetVimPendingState()` that ends the Normal-mode
dispatch — which is exactly what a half-typed count lives in.

The guard now rejects a null character as well as an empty string.

### This is the same trap as Return

[ADR 0076](0076-replace-a-character.md) recorded that Return arrives as
`U+0000` under the offscreen platform plugin and as `"\r"` under X11,
and that `r` therefore has to branch on the key code. This is the same
quirk reaching a different place: `event->text()` is not a reliable way
to ask whether a key carries a character.

That makes two bugs from one cause. Anything testing a key for "is this
typed input" should test the character, and anything distinguishing
Return, Tab or Escape from typed input should use the key code.

## Consequences

The screenshot battery had been encoding the bug as expected behaviour
since it was written: its `3G` step recorded a jump to line 829 of an
828-line file. Five of its twenty-two screens changed when this was
fixed, and the change is the fix. A regression suite is only as honest
as the behaviour it was captured against.

Whether the bug reproduces under X11 was not tested — Qt may report an
empty text there rather than a NUL. It does not matter much: the guard
is correct either way, and a bare modifier is never a command.
