# ADR 0128: One name, one command

## Status

Accepted

## Context

Two things turned up while writing the user documentation (ADR 0126),
both of which had to be documented as behaviour rather than fixed at the
time.

**`:editor.save` reported an unknown command.** A key binding resolved a
name through the viewport's registry, then the window's, then the plugin
host. The `:` line resolved it through the plugin host *only*. So
`key.ctrl+s = editor.save` worked and typing `:editor.save` did not —
the same name meaning two different things depending on how you said it.

**A plugin command destroyed the undo history.** A plugin is handed the
`AseBuffer` and edits it directly, so nothing it does passes through the
undo stack. The first version answered that honestly but bluntly:

```cpp
ase_undo_destroy(m_undo);
m_undo = ase_undo_create();
```

Running a formatter therefore cost every step back to the start of the
session, with no warning and no way to get it back.

## Decision

### One resolution order

`runCommandByName()` is the single answer to "what does this name mean":
this buffer's commands, then the window's, then any plugin's. Both the
key-binding path and the `:` line call it, so they cannot drift again.

`:` still takes its ex-commands first — `w`, `q`, `s/from/to/`, a bare
line number — because those are not commands in the registry and never
were. A name that is not one of those now reaches the registry instead
of skipping straight past it.

### The plugin's edit is recorded, not mourned

The text before the command is already in `m_cache`. The text after is
read back from the buffer. When they differ, the change is put back and
redone through the undo stack as one step.

`u` then takes back the whole command at once, which is what a single
command should cost, and everything before it survives. When they do not
differ, no step is recorded — a command that changed nothing must not
leave something for `u` to spend itself on.

## Consequences

`gui/tests/test_plugin_flow.cpp` is new: it writes a real Lua plugin into
the test config directory, loads it through the real plugin host, and
drives it through a real viewport. Three of its cases fail on the old
code.

That test is also the first automated coverage the plugin path has had
from the GUI side at all — ADR 0113's claim that a plugin command is
bindable like any other was true, but only demonstrated by hand.

The round-trip costs one copy of the buffer per plugin command. That is
the same cost the crash snapshot already pays per pause in typing, and a
plugin command is a deliberate, occasional act rather than something on
the keystroke path.

Nothing here changes what a plugin can *do*. The ABI is still one
function shape over a buffer; widening it is still
[EXTENSIBILITY.md](../EXTENSIBILITY.md)'s recommendation 2.
