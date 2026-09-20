# ADR 0134: Vim keys you can move

## Status

Accepted

## Context

[ADR 0113](0113-keybindings-as-data.md) made every chord data —
`key.<chord> = <command>` — and said plainly that Vim's own keys were
not part of it: `dd` and `gg` go through a stateful Normal-mode
dispatcher, not the chord table.

So the bindings a Vim user would most want to move are exactly the ones
that could not be moved. An editor whose headline is *keybindings as
data* should not have `hjkl` nailed down, least of all for someone on
Colemak or Dvorak.

## Decision

`vim.normal.<key> = <keys>`, which is vim's own `nnoremap`.

The right-hand side is a **key sequence**, not a command name and not a
single key. That covers both things people want:

```ini
vim.normal.n = j      # a layout remap
vim.normal.Y = y$     # the classic nnoremap
```

`vim.visual.<key>` is Visual only, and an unqualified `vim.<key>`
applies in both. The mode-qualified form wins, which is how `key.` has
always resolved.

### Not a data-driven dispatcher

The obvious reading of "vim sequences as data" is a table mapping every
Vim command to a name. That cannot work: `d2w` is an operator, a count
and a motion composed at the keystroke, not an entry in a table. A table
would have to enumerate the product of three open sets.

A remap sidesteps that entirely by standing in for the **key**, before
the state machine sees it. A count already typed and an operator already
pending then compose with it for free, because nothing downstream knows
a substitution happened — `3n` and `dn` work the moment `vim.normal.n =
j` exists, with no changes to counts or operators.

### Replayed, not re-entered

The right-hand side is fed back through the dispatcher one key at a
time — the same path `.` repeat and macros already use, which is why
this needed no new replay machinery.

Replaying happens with remapping **off**. That is the "nore" in
`nnoremap`, and without it

```ini
vim.normal.x = dd
vim.normal.d = x
```

is an infinite loop rather than two swapped keys.

### An argument is not a command

The key after `f`, `r`, `` ` ``, `"`, `q`, `@`, `i`/`a` and `g` is an
argument. Remapping it would make `f` unable to find a character you had
rebound, and `"ayy` would yank into whatever register `a` maps to.

`VimPending::expectsArgument()` names those states and the remap steps
aside for them. A pending **operator** is deliberately not one of them:
after `d` the next key is a motion, and a remapped motion composing with
a pending operator is the point.

## Consequences

Thirteen tests, of which seven fail without the hook and six are the
negative cases that must pass either way — argument keys untouched,
Insert mode untouched, an unmapped key unaffected.

The 220 vim conformance cases still pass unchanged, which is the check
that matters most: this sits on the hot path of every Normal-mode key
and must be invisible when nothing is configured.

**Measured** on that path: 4.49µs per Normal-mode key with the lookup,
4.27µs without — about 220ns, or 5%. Left alone rather than gated behind
a "has any remaps" flag. That gate was worth it for
[ADR 0125](0125-a-keystroke-should-not-allocate-to-ask-a-question.md),
where the waste was eight times the lookup it guarded; here it is a
twentieth of one keypress, and a Normal-mode key is a discrete act
rather than a burst.

A malformed remap is reported rather than ignored: `vim.normal.abc = j`
names no single key and would match nothing quietly, which is the
failure `keys::problems()` exists to prevent. Adding that check found
that the function returned early when a config had no `key.` settings at
all — so a config with only `vim.` ones went entirely unchecked.

What a key cannot be: `=`, because the config splits on it. Nothing else
in the format escapes, and inventing an escape for one key was worse
than the limitation.
