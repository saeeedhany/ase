# ADR 0036: Docs site branding (logo, editor colors) and a Plugins page

## Status

Accepted

## Context

Direct user feedback right after ADR 0035's docs site went live: the
site kept Material's default logo and default indigo/blue color
scheme instead of the editor's own identity, and there was no page
for the plugin host at all. The user also supplied a third color,
`#689d6a`, to use alongside the editor's existing two (`#282828`
background, `#F5E6C8` text) — the editor itself is deliberately
near-monochrome (ADR 0007's "one font color" pillar), so a docs-only
accent needed a real answer rather than reusing a color that means
something specific inside the editor itself (e.g. a diagnostic color).

## Decision

### Logo/favicon: the actual wordmark, not Material's book icon

`packaging/linux/ase-icon-512.png` (the padded-square version of the
hand-drawn "ase" logo already used for the app icon and About panel —
see ADR 0027, ADR 0034) is copied to `docs/assets/ase-logo.png` and
set as both `theme.logo` and `theme.favicon`. Copied rather than
referenced from `packaging/` directly: MkDocs resolves theme asset
paths relative to `docs_dir`, and keeping the site's own assets inside
`docs/` avoids a path that reaches outside the directory MkDocs
actually treats as its source tree.

### Colors: a custom Material palette, not a built-in named one

Material's `primary`/`accent` config keys only accept a fixed list of
named Material Design hues — there's no way to hand them an arbitrary
hex directly. The documented way around that is to set both to
`custom` and back them with real values via `extra_css`
(`docs/stylesheets/extra.css`), overriding Material's CSS custom
properties under `[data-md-color-primary="custom"]` /
`[data-md-color-accent="custom"]` / `[data-md-color-scheme="slate"]`.

- **Primary** (header bar, nav tabs) = `#282828`, the editor's own
  background, in *both* light and dark site-scheme variants — the
  header always reads as "the editor's chrome" regardless of which
  body scheme a visitor has picked.
- **Dark scheme body** = `#282828` background / `#F5E6C8` text,
  matching the editor exactly (this is the scheme most visitors get by
  default, since it follows `prefers-color-scheme`).
- **Accent** (links, search highlights, buttons, active-tab
  underline) = the new `#689d6a`. This color exists only in the docs
  site, not the editor itself — deliberately: the editor's own "one
  font color" pillar (ADR 0007) doesn't have a general-purpose accent
  hue to reuse, and diagnostic colors (`diagnostic_error`/
  `diagnostic_warning`, ADR 0029) already carry their own specific
  meaning inside the app, so borrowing one for "the docs site's link
  color" would misuse it. The docs site is allowed a third color the
  editor deliberately doesn't have.
- Light mode keeps Material's own default body background (better
  contrast/accessibility for anyone who prefers it) but still picks up
  the custom primary/accent, so it never shows Material's stock
  indigo.

### Plugins page: a real, honest placeholder

`docs/plugins.md` — a single admonition stating plainly that content
isn't written yet, linking to [ADR 0009](0009-plugin-abi-and-lua-host.md)
and `CONTRIBUTING.md`'s plugin-authoring guide as the actual source of
truth today, and to the roadmap for why it isn't wired into the GUI.
Added to the nav between Roadmap and Decisions. No placeholder
lorem-ipsum content — an honest "not written yet" note is more useful
than padding.

## Consequences

Verified by loading the rebuilt site in a browser (not just a clean
`mkdocs build --strict`, which doesn't catch a wrong logo or wrong
colors) and screenshotting: the header shows the actual "ase" wordmark
(confirmed by cropping and zooming into it — easy to mistake for the
default icon at a glance otherwise), links/nav/breadcrumbs render in
`#689d6a`, and the page background matches the editor's own
`#282828`/`#F5E6C8` exactly. The Plugins page renders its "coming
soon" admonition correctly and appears in the nav.

One mistake made and caught during this same verification pass, noted
here rather than silently corrected: a `Ctrl+L` keystroke aimed at the
docs-preview browser tab landed in a different, unrelated tab in the
same window (an old search tab from browsing history) and ran a stray
web search — not destructive, but a reminder that reusing an existing
browser window's active tab by keystroke, without first confirming
which tab is actually focused, is unreliable; opening a fresh tab via
`firefox --new-tab <url>` and re-locating it by window title afterward
is the more reliable pattern, used for the rest of this pass.

Not attempted: reskinning the light-mode body background to also
match the editor exactly — the editor has no light theme of its own
to match, so light mode stays a generic, accessible fallback rather
than an invented "light version" of a deliberately-always-dark editor
identity.
