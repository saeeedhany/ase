# ADR 0068: Freeing `Ctrl+O` and `Ctrl+I`

## Status

Accepted

## Context

ADR 0067 shipped go-to-definition and named the gap it created: no way
to jump *back*. Vim puts that on `Ctrl+O`, with `Ctrl+I` to jump
forward — and this editor had both: `Ctrl+O` opened a file, `Ctrl+I`
opened the About panel. ADR 0046 flagged the collision when it deferred
the jumplist, and ADR 0059 had already shown the shape of the
workaround: scope the override to Normal and Visual, as `Ctrl+D` does.

That workaround is wrong here. `Ctrl+D`'s override was safe because
multi-cursor editing happens while *typing*, so the override landed in a
mode where nobody used the old meaning. Normal mode is where you spend
your life, and opening a file is a fundamental action — taking it away
there is a real loss, not a technicality.

## Decision

Move the two panels instead. **A panel opened a few times a session can
afford an unusual binding; a navigation key used constantly cannot.**

| action | was | now |
|---|---|---|
| Keyboard shortcuts | `Ctrl+/` | **`F1`** |
| About | `Ctrl+I` | **`Alt+I`** |
| Open file | `Ctrl+O` | **`Alt+O`** |
| Jump back / forward | — | **`Ctrl+O` / `Ctrl+I`** (ADR 0069) |

`F1` is not a consolation prize: it is *the* help key, the one people
press without being told, and the panel should have been there from the
start. `Ctrl+/` was a fine mnemonic and the wrong key — and leaving it
unbound keeps it free for toggle-comment, which is what `Ctrl+/` means
in most editors.

About and Open move to `Alt`, together, so it is one rule rather than
two exceptions.

Two proposals were declined:

- **About on `F2`.** `F2` is the conventional *rename* key, and LSP
  rename is on the roadmap as part of the navigation half go-to-definition
  started. Spending it on the least-used panel in the app means either
  moving About twice or giving up the conventional key for a real
  feature.
- **Find moving to `Ctrl+/`.** `Ctrl+F` is the most universally known
  shortcut there is, and nothing is competing for it. Moving it costs
  muscle memory and frees nothing.

`Ctrl+Shift+O` still toggles the output panel, and `Ctrl+P` (ADR 0065)
is unaffected — in practice it is the opener people reach for anyway,
which is part of why moving `Ctrl+O` costs less than it would have a
week ago.

## Consequences

The rebinding lands on its own, before the jumplist that motivates it,
so that if `Alt+O` turns out to be awkward — tiling window managers
often claim `Alt` — it can be reverted without unpicking a feature.
Verified on this machine's WM: `F1` opens the shortcuts panel, `Alt+O`
the file browser, `Alt+I` About, and `Ctrl+O` now does nothing at all,
which is exactly the state ADR 0069 needs.

The welcome screen and the shortcut reference both list the new keys;
they were the two places that would otherwise teach the old ones.

This is the first time a binding has been *taken away* rather than
added. ADR 0026's "keybindings are compile-time constants" still holds,
and this is the cost of that: a rebinding is a code change and a
documented decision rather than a line in a config file. Keybindings as
data remains the Tier 2 roadmap item it has been since then, and this
ADR is one more argument for it.
