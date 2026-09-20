# ADR 0133: A theme you can share as a file

## Status

Accepted

## Context

[ADR 0114](0114-built-in-themes.md) shipped three palettes and the
layering that makes `:theme` honest — a colour you set by hand survives
a theme, and previewing one produces the same screen as saving it.

It did not ship a way to add a fourth. The Themes page said so in its
own words: *"There is no theme file format and no theme directory — a
theme is just those keys, so sharing one means sharing nine lines."*

For an editor whose description begins "aesthetically deliberate", three
fixed palettes and no way to add one is friction a user meets in the
first hour.

## Decision

`<config dir>/themes/<name>.ase`, beside `plugins/` and `recovery/`.
The name comes from the file; the contents are the same `key = value`
the config uses.

### A theme file is a config file

`ase_config_load()` parses it, which means the format needs no second
implementation and no second parser to keep in step.

It also gives the inheritance for free: that function overlays a file
onto the shipped defaults, so a key a theme omits already reads as the
default. **Changing two colours does not mean restating nine** —

```ini
summary = Deep blue with warm accents
background = #0B1021
text = #D8DEE9
```

is a complete theme, and the accents come from what ships.

### Loaded, not merged

`ase_theme_load_directory()` **replaces** whatever a previous call
loaded rather than adding to it. A config reload calls it again, and a
set that only ever grew would keep a theme you had deleted.

A file named after a built-in **shadows** it rather than sitting beside
it — you put it there on purpose — so the count does not grow and
`:theme` does not list the name twice.

### Read wherever the answer must be current

`rebuildConfig()` reloads the directory, which covers startup and a
config hot-reload. That alone was not enough, and the GUI run is what
showed it: dropping a file in while the editor is running does not touch
`config.ase`, so nothing triggers a re-read, and `:theme latecomer`
answered *"no theme called 'latecomer'"* for a theme that was sitting
right there.

`runTheme()` now reloads before listing or looking up. `:theme` is a
rare, deliberate action and the directory is small, so re-reading it
there costs nothing and means the list is never stale.

## Consequences

`AseTheme` was a table of string literals and is now sometimes owned
memory, so the loaded set carries its own storage and
`ase_theme_unload()` releases it. The tests call it; the GUI does not,
and the set is reachable at exit rather than leaked.

Six tests cover the parts with edge cases: a file becoming a theme, a
partial file inheriting, a file shadowing a built-in, non-`.ase` files
being ignored, reloading replacing rather than accumulating, and a
missing directory not being an error.

Verified in the editor as well as in tests. `:theme` lists
`[ase-default] simple-nord simple-sola inkwell`, applying one repaints,
and `:theme save` writes `theme = inkwell`.

The starter config now says where themes live, which is the only place
someone would look before the documentation.

What this does not add: a light theme still needs more than colours —
the panel alpha, the dimmed comment tier and the gutter are all tuned
for a dark ground (ADR 0114), so a light theme file will be a theme
whose background is light and whose chrome is still wrong. That is a
second set of rules rather than a file format.
