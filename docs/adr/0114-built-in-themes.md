# ADR 0114: Built-in themes

## Status

Accepted

## Context

Every colour was a hex value in `config.ase`, nine of them, and the
starter file set all nine. Someone installing the editor either lived
with the default palette or picked nine colours themselves. There was no
middle step.

## Decision

### Three palettes, six colours each

```
                 bg        text      type      string    error     warn
ase-default      #282828   #F5E6C8   #689D6A   #D79921   #E06C75   #E5C07B
simple-nord      #2E3440   #D8DEE9   #8FBCBB   #EBCB8B   #BF616A   #EBCB8B
simple-sola      #002B36   #93A1A1   #2AA198   #B58900   #DC322F   #B58900
```

Nord and Solarized each define sixteen colours. Taking all sixteen would
make this editor look like every other one that ships them; taking the
ground, the text, and the two that pair is what keeps it looking like
itself in someone else's palette — the pillar is one background, one
text colour and two accents (ADR 0007, ADR 0048), and a theme has to
respect that rather than route around it.

Error and warning come from the palette too, so a Nord editor does not
flash an Atom-red underline. The chrome — selection, find match, panel —
is each palette's own selection colour (Nord's `#434C5E`, Solarized's
base02 `#073642`) rather than something computed from the background,
which is both simpler and more correct than a derivation would be.

Solarized is dark only. The panel alpha, the dimmed comment tier and the
gutter are tuned for a dark ground; a light theme is a second set of
rules rather than a second row in the table.

### A theme yields to a colour someone chose, not to one they were given

`theme = simple-nord` supplies the palette, and a colour in `config.ase`
wins over it — but only if it has actually been changed. A value still
equal to what the starter file shipped is not a decision, and a theme
may replace it.

The first version of this rule was "anything in the file wins", which is
the obvious rule and was wrong in practice. Every `config.ase` written
before themes existed contains all nine colours, because the starter
file wrote them out; treating those as choices made `:theme` a no-op for
everyone who had ever run the editor. It was reported within minutes of
being built, against exactly that config.

Being in the file is not the same as having been chosen. Commenting the
colours out of the starter file fixes it for new installs; this fixes it
for existing ones, which is the larger group and the one that was
actually broken.

This is also what makes `:theme` honest. A session theme layers in
exactly the place `theme =` does, so previewing a palette and saving it
produce the same screen.

### `:theme` lists, switches, and saves separately

`:theme` lists them with the active one bracketed. `:theme <name>`
switches for this session. `:theme save` writes the choice into
`config.ase`, replacing the existing assignment and leaving every other
line and comment alone.

Switching does not touch the file. Trying palettes should not edit
something the user also hand-edits, and the save is one more word.

## Consequences

**The starter file no longer sets the colours.** It used to set all
nine, and that would have made every theme a no-op for every user: a
theme yields to a colour set by hand, and a shipped file that sets all
nine means every colour is set by hand. They are shown as comments with
their default values, which documents them just as well and leaves the
theme something to do.

This was found by driving the real editor rather than by reading the
code — the palette switched and the background did not move. A test now
fails if any colour key reappears uncommented in the starter file, and
that test was checked by putting one back.

**A colour genuinely changed still shadows the theme**, and `:theme
<name>` names it when it does: *"simple-nord — your config still sets
text"*. That turns "this theme is broken" into "oh, that is my line",
the same reasoning as the `no highlight (large file)` label in ADR 0107.
Because unchanged defaults no longer count, this now fires rarely and
means something when it does.

The comparison is case-insensitive: the starter file writes
`syntax_type = #689d6a` and the theme table `#689D6A`, and a rule that
called those different would have left exactly the two accent colours
un-themed while everything else changed.

Eleven core tests cover the layering, including a config containing all
nine shipped colours (the reported case), one genuinely changed colour
among them, switching themes twice leaving nothing of the first behind,
and `ase-default` being exactly the defaults — otherwise selecting it
would change the editor's look.

One subtlety the tests caught: `from_file` only ever accumulated, so a
theme taking a slot left the entry still marked as the user's, and the
theme's own colour then looked hand-chosen to anything that asked. A
theme that legitimately takes a slot clears the flag.

What this is not is theme *files*. `EXTENSIBILITY.md`'s recommendation 5
wants `theme = gruvbox` loading `~/.config/ase/themes/gruvbox.ase`, and
that is still open; this is the built-in half, which is what someone
needs in the first five minutes.
