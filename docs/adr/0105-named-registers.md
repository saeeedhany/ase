# ADR 0105: Named registers, and a test that was lying

## Status

Accepted

## Context

There was one register. `"` was not handled at all, which made it worse
than missing: typing `"ayy` left `"` doing nothing, then `a` entered
**Insert mode** and `yy` was typed into the file. A command that does not
exist yet should do nothing, not edit the buffer.

```
before:  foo.bar baz
"ayyjP:  fyyjPoo.bar baz
```

That is the only defect found in this work that loses the user's text.

## Decision

### `"x` names a register for the next yank, delete or paste

Registers `a`–`z`, with an uppercase name appending to the same slot.
Verified against vim, same file and keys:

| keys | |
|---|---|
| `"ayy` … `"ap` | yank to `a`, paste from `a` |
| `"add` … `"ap` | delete to `a` |
| `"ayy` … `"Ayy` … `"ap` | uppercase appends |
| `"ayy` … `p` | a named yank **also** fills the unnamed register |
| `"ayw` … `"aP` | charwise, pasted before |
| `"ayy` … `"byy` … `"ap"bp` | two registers side by side |

### The name has to outlive the pending-state reset

`vimApplyPendingOperatorCharwise` and its linewise twin read the operator
and then call `resetVimPendingState()` **before** doing the work, so a
register named on the pending state would be gone by the time the yank
ran. They park it in `m_vimRegisterInUse` first; everything else still
has `m_vimPendingRegister` live when it needs it. Either way
`vimTakeRegister()` consumes it exactly once, so a name never leaks into
the following command.

`"` is recognised above the guard in `vimApplyNormalKey` that abandons a
pending operator — the fourth feature to need that, after marks, `d'a`
and text objects.

### The suite found a bug in the suite

`"aVy` then `"ap` pasted in our editor and did nothing in vim, and the
editor was **right**: vim consumes the register prefix on the next
command, even a mode change, so `"aV` wastes it and `y` goes to the
unnamed register. Ours agreed.

The failure was that registers are process-wide statics and the test runs
every case in one process, so an earlier case's `"ayy` was still sitting
in `a`. vim starts each case in a fresh process. `VimRegister::clearAll()`
now runs per case.

That mattered more than the case that exposed it: **any** register-
dependent expectation could have passed or failed for the wrong reason.
A suite that shares state between cases is not measuring what it says.

## Consequences

120 cases now, 33 ms.

**Macros and registers remain separate stores.** In vim they are the same
thing — `qa` records into register `a`, and `"ap` pastes the recording as
text. Here macros hold `RecordedKey` structs (key code, modifiers, text)
because [ADR 0097](0097-marks-and-macros.md) needed `Escape` and
`Backspace`, which carry no text and would be lost by a text register.
Unifying them means encoding keys as text the way vim does, and that is
its own change.

Numbered registers (`"0`–`"9`), the small-delete register `"-`, the
black-hole `"_` and the clipboard registers `"+`/`"*` are not
implemented. `"+` is the notable absence: yank still mirrors to the
system clipboard unconditionally, which covers the common case by
accident rather than by asking.
