# ADR 0140: The styles a capture can have

## Status

Accepted

## Context

[ADR 0007](0007-syntax-highlighting-tree-sitter.md) set the pillar:
syntax is one hue, and the categories are told apart by weight, slant and
opacity rather than by colour. It gave a table — keyword bold, type
italic, string and number at ~78%, comment at ~45%.

Everything in that table was a literal in `EditorViewport`.
[ADR 0048](0048-syntax-accent-colors-caret-inset-status-init.md) later
gave types and strings a real hue each and made *those* two
configurable, which left the arrangement backwards: the two deliberate
exceptions to the pillar could be configured, and the pillar itself
could not. Someone who found the comments too faint had no way to say
so.

## Decision

Four keys, in the same table every other setting lives in:

| key | default |
| --- | --- |
| `syntax_keyword_bold` | `true` |
| `syntax_type_italic` | `false` |
| `syntax_comment_opacity` | `57` |
| `syntax_number_opacity` | `78` |

Opacity is a percentage rather than an alpha byte, because a config file
is read by people. The defaults reproduce the previous literals exactly:
57% of 255 is 145, which is the value
[ADR 0048](0048-syntax-accent-colors-caret-inset-status-init.md) picked
over ADR 0007's 45% because 45% measured 3.64:1, below WCAG AA. That
reasoning now sits next to the default it produced.

`syntax_type_italic` defaults **off**, which is a divergence from ADR
0007's table rather than a restoration of it. Types carry a hue now, and
slant on top of colour is louder than either alone. The key exists for
people who want ADR 0007's original scheme back.

### The metrics have to agree with the font

`fontForCapture()` says what a run is drawn with, and
`metricsForCapture()` says what it is measured with — for run widths and
for the caret. [ADR 0124](0124-the-debounce-that-never-happened.md) is
about what happens when those two disagree: the caret sits somewhere the
text is not. So italic needed a cached `QFontMetrics` of its own, on the
same branch condition as the font, and both functions now test the same
config flag rather than the same hardcoded capture.

## Consequences

No new tests, and that is a deliberate call rather than an omission. The
schema side is already covered by tests that do not need to know these
keys exist: one asserts every documented key's default is what a fresh
config returns, another that the starter file mentions every key. Both
covered these four the moment they were added.

What those cannot check is whether the editor draws differently, so that
was verified by looking: types italic and keywords plain under one
config, comments at full strength under another, and the default
rendering unchanged against the same file. Glyph spacing is correct in
each, which is the metrics-agreement check above.

This closes the last of the paper cuts ROADMAP had accumulated.
