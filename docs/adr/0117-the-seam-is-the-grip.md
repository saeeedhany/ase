# ADR 0117: The seam is the grip

## Status

Accepted

## Context

The join between the editor and the output panel was a short centred bar
— 40×2 pixels with six pixels of padding above and below it. It read as
an object placed on the layout rather than as the line where one region
ends and the next begins, and it did nothing: the panel's height was
whatever the layout decided.

## Decision

### A hairline, edge to edge

One pixel, full width. A seam runs the width of what it separates; a
short bar in the middle is a handle sitting on top of the join rather
than the join itself.

### Dim until attended, then full

Alpha 22% at rest, easing to 100% when the pointer is over it, on the
existing fade tier (`motion::kFade`). A divider is not something to look
at while reading code — the same reasoning that dims the gutter in
ADR 0014 — but it has to be findable the moment you go looking for it.

Measured, because "looks dimmer" is not a check: the seam's row reads 85
in luminance unhovered and 245 hovered.

### The line is one pixel; the widget is seven

Nobody can reliably hit a one-pixel target. The visible line is a
hairline and the grab area around it is seven pixels tall, which is the
standard trick and the reason the line can be this thin at all.

### Dragging resizes, and so do keys

Dragging the seam moves the panel's edge with the pointer. The editor
above keeps a floor, so the panel cannot be dragged over the whole
window leaving nothing to grab it back by.

`Ctrl+Shift+Up` and `Ctrl+Shift+Down` do the same in 60-pixel steps,
eased on `motion::kChrome`. They are **window** commands, not viewport
ones: resizing the panel is most wanted while reading it, and the panel
has focus then — a viewport binding would never see the key. That was
found by trying it, not by reasoning about it.

Dragging is not eased. Animating a drag makes the edge lag the pointer.

### Keyboard navigation scrolls the way the wheel does

`Ctrl+J`/`Ctrl+K` already moved the selection (ADR 0069), and the wheel
already glided (`installSmoothScroll`, ADR 0031) — but that filter only
sees wheel events, so moving the selection out of view snapped. The
arrows and `Ctrl+J`/`Ctrl+K` now ease the viewport to where the list
wants it.

## Consequences

Verified by driving the real editor: the seam brightens by 160 levels of
luminance on hover, two presses of `Ctrl+Shift+Up` grow the panel by
exactly 120 pixels, and dragging the seam up 100 pixels moves it 100
pixels.

None of that was possible before: the run-ase driver could send keys but
not move the pointer, so hover and drag had no way to be tested. It has
`pointer()` and `drag()` now.

`TranslucentBar` is no longer used by the output panel. It is still the
file browser's row highlight, so it stays.
