# ADR 0125: A keystroke should not allocate to ask a question

## Status

Accepted

## Context

Every key press asks two questions before anything else happens: is
this chord the first half of a two-chord sequence (`keys::isPrefix`),
and what command is it bound to (`keys::commandFor`). Measured at 20,000
iterations:

| | ns per key press |
| --- | --- |
| `isPrefix` | 4584 |
| `commandFor` | 537 |

`isPrefix` cost eight times the lookup it guards, to answer "no" for
almost every key.

## Decision

It built two `QStringList`s of every `key.*` setting in the config, then
ran `ase_keymap_canonical` over each one and over all sixty rows of the
built-in table — per key press, to find the handful of rows containing a
`>`.

Two things were wasted. The built-in table never changes, so its
sequences are now split once into a function-local static. And a setting
without a `>` cannot be a sequence, so a `strchr` now decides that
before any `QString` exists: a config with no sequence bindings — which
is most of them — allocates nothing at all.

`isPrefix` fell to 212ns.

## Consequences

The three sequence functions (`isPrefix`, `commandForSequence`,
`sequenceHints`) now share one `Sequence` shape and one way of reaching
it, instead of each re-deriving the split inline.

`commandFor` was left alone. Assembling its setting name on the stack
with `qsnprintf` instead of `QStringLiteral(...).arg(...)` was tried and
**measured slower** — 1030ns against 752 for the pair — so it was
reverted. Qt's short-string paths beat `vsnprintf` here; the obvious
optimisation was the wrong one.

This is not where typing time goes. At 13KB of C a keystroke is ~800us,
so 4.4us is half a percent. It matters for a file with no grammar, where
a keystroke is 8us and this was most of it, and it is worth having
because the cost bought nothing at any size.

`lookupStaysOffTheAllocator` asserts `isPrefix` stays cheaper than
`commandFor`. A ratio rather than a number of nanoseconds, because an
absolute bound fails under a sanitizer — the first version of this test
did exactly that, and its early return then leaked the config it had
built.
