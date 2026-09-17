# Reference

Three tables, generated from the editor's own source every time these
docs are built. They describe what the binary actually does, not a
description of it that someone remembered to update.

- **[Every command](commands.md)** — every action the editor can be asked
  to perform, and the keys that reach it. Read from the command registry.
- **[Every default binding](keybindings.md)** — the built-in keyboard
  table in full. Read from `keys::defaults()`, the same table the editor
  dispatches from and the `F1` panel displays.
- **[Every config key](config.md)** — every setting, its default and what
  it means, plus the built-in themes. Read from the table in
  `core/src/config.c` that also produces the starter config file.

If you want to know *why* something is the way it is rather than what it
is, that is what [Decisions](../adr/index.md) is for.
