# ADR 0115: One path for a colour change

## Status

Accepted

## Context

Reported: `:theme simple-nord` recoloured the text area, and the tab bar
and status bar stayed as they were until the caret happened to move.

The window's chrome is repainted by the `statusChanged` handler in
`main.cpp` — that is where `applyStatusBarTheme()` and
`refreshBufferBar()` live. `statusChanged` is emitted by
`ensureCursorVisible()`, so anything that wants the chrome repainted has
to call it, whether or not the cursor has moved.

`checkConfigReload()` ends with `ensureCursorVisible(); update();` and a
chain of eight `refreshTheme()` calls, one per panel. That is why
hand-editing `config.ase` had always worked.

`runTheme()`, added with the themes in ADR 0114, ended with `update()`.
It repainted the viewport and nothing else — not the chrome, and not any
of the eight panels either, which would have kept their old colours the
next time one was opened.

## Decision

The refresh becomes one function, `repaintForNewTheme()`, called by both.

There are two ways the colours can change — the config file being edited
and `:theme` — and the second was written without half of what the first
does. Two call sites that must do the same nine things are a defect
waiting for a third.

`ensureCursorVisible()` stays in it with a comment saying why. In a
function about repainting it reads like a stray scroll, and that is
presumably how it came to be left out.

## Consequences

Verified by switching theme and screenshotting **without pressing
another key**, which is the state the report was about, against builds
with and without the fix: before, the text area is Nord and the bars are
still the old near-black; after, all of it changes at once.

The panels were also never refreshed by `:theme` and now are. Nobody
reported that, because a panel has to be opened to be seen and opening
one is exactly the kind of event that emitted `statusChanged` anyway.
