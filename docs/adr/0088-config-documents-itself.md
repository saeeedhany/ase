# ADR 0088: Config keys document themselves

## Status

Accepted

## Context

`ase_config_write_default_if_missing()` writes a commented starter file
on first run and never touches it again
([ADR 0008](0008-config-theme-format.md), decision 5). That starter
file was the only user-facing description of what config keys exist.

Which means it is documentation with an expiry date. A user who first
ran the editor in September has a file describing September's keys, and
every key added since is invisible to them — there is no other list. The
three changes before this one added `lang.<id>.lsp`, `filetype.<suffix>`
and `.ase.conf`, none of which appear in an already-written file.

Asked "where is the config file, and how do I find out what I can put in
it", the honest answer was that you read the source. For a pillar named
Minimal that is the wrong kind of minimal.

There was a second, quieter version of the same gap: `animations` and
`line_numbers` had no entry in `ase_config_create_default()`. Their
defaults lived in the GUI code that read them, as fallbacks for a NULL
lookup. Nothing was wrong with the behaviour, but "what is the default"
had two possible places to look and no single answer.

## Decision

### One table, and everything derives from it

`AseConfigKeyDoc` — key, default, one-line summary, whether a project
file may set it — is now the single list, and `ase_config_key_docs()`
hands it out. From it come:

- the shipped defaults, which `ase_config_create_default()` loops over
  rather than listing by hand;
- the editor's own reference, built at runtime;
- the tests below.

The point is not reuse. It is that a key cannot be added without a
summary, because the struct has a field for one and the test fails when
it is empty. Documentation that is a required argument does not drift.

This is the second time this codebase has reached for the same fix:
`helpSections()` is data rather than an HTML literal because the literal
"drifted badly out of date". Same disease, same treatment, one layer
down.

### The reference is in the editor, not in a file on disk

F1 gains a Configuration section listing every key with its summary,
generated from the table, with the config file's own path as the section
note. It is correct by construction for the binary you are running,
which a written-once file cannot be.

Search now narrows a section to its matching rows instead of showing the
whole section. That was tolerable for a six-row keybinding block and is
not for a nineteen-row key list: a list you can only locate is not the
same as a list you can search.

Generated rows are HTML-escaped. The rows are rich text, and the first
two generated keys were `lang.<id>.lsp` and `filetype.<suffix>` — both
of which silently lose their angle brackets to the rich-text parser
otherwise. The hand-written rows keep writing their own entities, which
is why the escaping belongs at the point a row is generated rather than
where it is rendered.

### `:config` opens the config file

The "where is it" question deserves an answer shorter than a path. It
writes the starter file first if it has gone missing, so the command
always lands on something editable.

### Two tests, guarding the two ways this rots

- every key in the table has a non-empty summary, and its `project`
  flag agrees with `ase_config_key_allowed_in_project()` — otherwise
  the panel would describe a scope the parser does not enforce;
- the generated starter file mentions every key in the table, so a new
  user's file is complete even though the in-app reference is now the
  authority.

## Consequences

**An existing config file is still never rewritten, so it stays stale.**
That is deliberate — silently editing a file the user owns to insert
comments is worse than the problem. The fix is that the stale file no
longer matters: F1 is the reference, and a user who wants the current
commented template can delete theirs and reopen the editor. The starter
file's job narrows from "the documentation" to "a working example to
start from".

Moving `animations` and `line_numbers` into the table gives them real
defaults where they previously had none. Both callers were checked: a
NULL lookup meant `false` and `"absolute"`, and the table now supplies
exactly those strings, so behaviour is unchanged. Anything added to the
table later must be checked the same way — a default that appears where
code expected NULL is a behaviour change wearing a documentation
costume.

The summaries have to fit one line in a panel, so they say what a key
does and not why. The why stays in the ADRs, and a key whose reasoning
matters should point at one.
