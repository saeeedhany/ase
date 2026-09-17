# Themes

A theme is nine colours: a background, a text colour, two syntax accents,
a selection, a find match, a panel background, and an error and warning
colour. That is the whole palette, deliberately — this editor's aesthetic
is one background, one text colour and two accents, so a "simplified
Nord" is Nord's ground, its text, and the two of its sixteen colours that
pair.

Error and warning come from the palette too, so a Nord editor never
flashes an Atom-red underline.

## Trying one

```
:theme                 list them; the current one is in [brackets]
:theme simple-nord     apply it now, nothing written
:theme save            keep it
```

Applying and saving go through the same code, so previewing a theme and
keeping it produce the same screen. Nothing touches your config until
`:theme save`, which writes a single `theme = <name>` line.

The built-in palettes are listed in
[Every config key](../reference/config.md#built-in-themes).

## What a theme will not touch

A colour you set by hand in your config stays yours. The rule is that
**being in the file is not the same as having been chosen**: a value
still equal to what shipped is not a decision and a theme may replace it,
but a value you actually changed wins over the theme.

This is worth stating because it was once wrong in the other direction.
The starter config wrote all nine colours out, so every config file was
full of values its owner had never picked — and treating those as choices
made every theme a silent no-op. See
[ADR 0114](../adr/0114-built-in-themes.md).

## Writing your own

Set the nine keys in your config and leave `theme` unset:

```ini
background       = #1C1C1C
text             = #D0D0D0
selection        = #303030AA
find_match       = #3A3A3AAA
panel_background = #1C1C1CE6
syntax_type      = #87AFAF
syntax_string    = #D7AF87
diagnostic_error = #D75F5F
diagnostic_warning = #D7AF5F
```

Colours are `#RRGGBB` or `#RRGGBBAA`. The alpha channel is what makes the
panels and the selection sit *over* the text rather than replacing it.

There is no theme file format and no theme directory — a theme is just
those keys, so sharing one means sharing nine lines.
