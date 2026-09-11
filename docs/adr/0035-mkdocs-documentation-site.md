# ADR 0035: MkDocs documentation site

## Status

Accepted

## Context

Documentation (`docs/SPEC.md`, `docs/ROADMAP.md`, 34 ADRs at this
point) had accumulated as plain markdown, browsable only by reading
files directly on GitHub — no search, no cross-linking beyond manual
`[text](path.md)` links, no single browsable "here is the project's
whole history" view. User asked for a proper documentation website,
specifically calling out showing "the history of everything."

## Decision

**MkDocs + Material for MkDocs**, deployed to GitHub Pages. Chosen
over mdBook or Docusaurus: the project's docs were already plain
markdown with no non-standard syntax, so there was nothing to migrate;
Material's theme is mature and needed zero custom CSS to look right;
and `mkdocs gh-deploy` is a single command that builds and pushes to
the `gh-pages` branch, which GitHub auto-detected and enabled as a
Pages source with no manual settings-page step.

### `docs/` stays the source — nothing moved

MkDocs defaults to reading from a `docs_dir` of `docs/`, which this
project already had. Rather than restructure anything, `mkdocs.yml`
just points at it as-is: `docs/SPEC.md`, `docs/ROADMAP.md`, and
`docs/adr/*.md` are the site's source files, unchanged, still the
same files a contributor reads directly on GitHub. Two new files
support the site itself: `docs/index.md` (the site's home page — the
repository's own `README.md` is a different, GitHub-facing document
with build instructions the docs site doesn't need) and
`docs/adr/index.md` (a generated table of every ADR number and title,
in order — this *is* the "history of everything" view the request
asked for, and the thing this whole change was really for).

### Every ADR gets an explicit nav entry, not directory auto-discovery

`mkdocs-awesome-pages-plugin` was tried first, for directory-shorthand
nav (`Decisions: adr`) so 34+ files wouldn't need listing individually
in `mkdocs.yml`. It doesn't support that shorthand — `mkdocs build`
rejected it outright ("a reference to 'adr' is included in the nav
configuration, which is not found in the documentation files").
Rather than debug the plugin's actual directory-nav mechanism further,
switched to an explicit nested list (`adr/index.md` plus all 34
`adr/NNNN-*.md` entries) and dropped the plugin — one more dependency
than necessary for what MkDocs's own explicit `nav` already does
natively. The tradeoff, accepted deliberately: adding ADR 0036 later
means adding one line to `mkdocs.yml`'s nav, not something that
updates itself. `docs/adr/template.md` (not a real decision, just the
boilerplate for writing new ones) is excluded from the build entirely
via `exclude_docs` rather than included and awkwardly worked around.

### Two real rendering bugs found by actually looking at the page

- The home page's button links
  (`[:octicons-mark-github-16: Source on GitHub](...){ .md-button }`)
  rendered as literal text — icon shortcode and all — instead of
  styled buttons, because the `attr_list` (for `{ .md-button }`) and
  `pymdownx.emoji` (for the icon shortcode, using Material's own
  `twemoji`/`to_svg` extension config) markdown extensions weren't
  enabled. Material's button/icon syntax needs both explicitly listed
  in `mkdocs.yml`; it doesn't enable them on its own.
- `mkdocs serve`'s dev server serves under a `/ase/` path prefix
  (derived from `site_url`, matching the real GitHub Pages project-site
  URL) — every manual verification request against a bare
  `127.0.0.1:8765/...` path 404'd until that prefix was added, which
  looked like a broken nav at first before checking the server's own
  log.

Both were caught by actually loading the built site in a browser
(Firefox via the existing X11 display, screenshotted) rather than
trusting a clean `mkdocs build --strict` run alone — a strict build
catches broken links and missing nav targets, not markdown extensions
silently failing to activate a feature.

### Reproducibility

`docs/requirements.txt` pins `mkdocs`/`mkdocs-material` to the exact
versions used here. Docs tooling lives in a project-local virtualenv
(`.venv-docs/`, gitignored) rather than installed system-wide — this
machine's `pip` refuses unmanaged global installs (PEP 668), and a
project-local venv is the right answer regardless of that: it keeps
docs-build dependencies from leaking into (or colliding with) anything
else on a contributor's machine.

## Consequences

Live at <https://saeeedhany.github.io/ase/>, verified by loading it
over the real network (not just the local dev server) and confirming
both the home page and a sample ADR page (`/adr/0034-linux-packaging/`)
return `200` with correct content. `README.md` now points at the site
as the primary way to browse docs, keeping the raw markdown links as a
fallback for anyone reading directly on GitHub.

Not attempted: versioned docs (`mike`) — this project has no released
versions with diverging docs yet, so it's not earning its complexity
budget; a CI job to auto-deploy on every push to `main` (today's
deploy was a manual `mkdocs gh-deploy` run — a real gap if docs and
code drift out of sync before the next manual deploy, tracked as a
follow-up, not solved here).
