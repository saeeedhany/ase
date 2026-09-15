# ADR 0087: A project file says what files mean, never what to run

## Status

Accepted

## Context

[ADR 0086](0086-one-language-gate-and-per-language-keys.md) added
`filetype.<suffix>`, which fixes `.h` in a C++ project, and then noted
that it fixes it in the wrong place: config is user-level, so
`filetype.h = cpp` applies to your C projects too. Whether `.h` means C
or C++ is a fact about a repository, and it belongs with the
repository.

Reading config out of the repository is the part that needs care. A
config file a repository ships is a file an author you have never met
wrote, and `lsp_command` and `build_command` name programs the editor
executes. Honouring those from a checkout means cloning something and
opening a file is enough to run its author's code.

This is vim's `exrc` problem. Vim's answer is to ship it off by default
and add `secure` to restrict what a local file may do; neovim's is a
trust prompt keyed on the file's hash. Both are machinery, and both
exist because the local file was allowed to name commands in the first
place.

## Decision

### The rule comes before the file

A project config may set what files **mean**. It may never set what
commands to **run**.

Everything else follows from that, and the reason to state it first is
that it is what makes the trust machinery unnecessary. There is no
prompt, no hash database, no `secure` mode and no opt-in switch,
because there is nothing dangerous for a project file to say. It does
not need to be trusted; it needs to be unable to do harm.

That rule also falls along a line the keys already had. `filetype.h`
describes the repository — it is the same answer for everyone who
checks it out. `lang.cpp.lsp` describes the machine the checkout is on
— which of clangd, ccls or nothing at all you happen to have
installed. They were never the same kind of setting; one of them is
simply also the dangerous one.

### Default-deny, not a list of banned keys

`ase_config_key_allowed_in_project()` accepts `filetype.*` and refuses
everything else. It is an allowlist rather than a denylist on purpose:
a denylist means the next key someone adds is silently permitted in
project config, and a key that executes something would open the hole
back up without anyone noticing. With an allowlist the next key is
refused until somebody decides otherwise, and deciding is the point.

So today a project file can set exactly one family of keys. That is a
small surface for a whole file, and the right size to start from —
indentation is the obvious next candidate, once there is any.

### The nearest `.ase.conf` at or above the file

Found by walking up from the file's own directory, first hit wins. No
merging of several project files and no `root = true` marker: one file
answers, or none does.

Re-resolved on the same timer that already polls the user config
([ADR 0008](0008-config-theme-format.md), decision 4) rather than
remembered from open, so a `.ase.conf` that is created or changed while
the editor is running takes effect — including rebuilding the grammar
when the language changes, which is the visible half of the feature.
It costs a handful of `stat()`s next to the one already being made.

### A refused key is reported, not dropped

If a project file sets something it may not, the status bar says how
many keys were ignored. A silently ignored setting is precisely the
failure this line of work has been about — a `.cpp` file with no colour
because two tables disagreed, a query that failed to compile and looked
like an unsupported language. A project file whose `lsp_command` does
nothing, with no explanation, would be the same bug wearing a third
costume.

The message is queued rather than emitted directly, because the first
overlay happens in `EditorViewport`'s constructor, before anything is
connected to `messagePosted` — emitting there would have dropped the
very message whose absence this section is about.

## Consequences

A project cannot configure its own language server, and that is the
trade being made rather than an oversight. A repository that needs
`clangd --header-insertion=never` cannot ask for it; the user sets it
per-language on their machine. If per-project commands are ever really
wanted, they need the trust machinery this ADR was written to avoid,
and that is a separate decision with a separate cost.

`filetype.*` in a project file wins over the same key in the user file,
which is the one place project config overrides the user rather than
the reverse. That is correct for this key — the repository knows what
its own `.h` files are, and the user's global guess does not — but it
is a precedent to apply carefully to anything added to the allowlist
later.

The file is found by directory, not by version control, so it works in
a directory that is not a repository at all, and a `.ase.conf` in
`$HOME` applies to everything beneath it. That is a consequence of
having no project concept beyond "the nearest file that answers", which
is the whole of the design.
