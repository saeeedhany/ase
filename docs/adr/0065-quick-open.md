# ADR 0065: Quick open (`Ctrl+P`)

## Status

Accepted

## Context

Multiple buffers landed in ADR 0054, but the only way to *get* a buffer
was ADR 0023's file browser: one directory at a time, navigating with
`..`. That is fine for the file next to the one you have open and
tedious for anything else, which in practice means the editor is
pleasant to edit in and awkward to move around in.

## Decision

### `Ctrl+P`, as a third mode of the panel that already exists

Not a new widget. Quick open is the same badge, the same input field,
the same list, the same sliding row highlight, the same theming and the
same Enter/Escape handling as Open and Save-As — it differs only in
where the entries come from and how they are filtered. A separate class
would have been roughly two hundred duplicated lines that then drift
apart visually, which is how editors end up with two file pickers that
look almost the same.

So `FileBrowserPanel::Mode` gains `QuickOpen`, the badge shows `P`, and
three methods branch on it.

### "The project" is the git checkout, or the folder you are in

`project::rootFor()` walks up from the current file's directory looking
for `.git` and stops at the first one; with no repository anywhere
above, the file's own directory is the root.

Deliberately **not** `git ls-files`. It would be faster on a huge
repository and would respect `.gitignore` for free — and it would also
mean `Ctrl+P` silently doing nothing in a directory that isn't a
checkout, which is a perfectly normal way to use a text editor.

The walk prunes rather than filters: an explicit directory stack, not
`QDirIterator::Subdirectories`, because that descends into everything
and only lets you drop the *results* — on this repository that means
walking all of `.git` to throw it away. Hidden entries and a fixed list
of build/vendor directory names (`build*`, `node_modules`, `.venv*`,
`target`, `dist`, …) are skipped. It is not a `.gitignore` parser and
does not pretend to be one; it is a fixed list, which never surprises
you by hiding a file you can see in your own directory listing.

### The score is the feature

Matching is subsequence — `edvim` finds
`gui/src/editor_viewport_vim.cpp` — but with a few hundred files, half
of them match any short query, so ranking is the entire job. Four
things, in descending weight: consecutive **runs**, characters matched
at a **boundary** (`/`, `_`, `-`, `.`, or a camelCase hump), matches in
the **basename** rather than a directory name, and **brevity** as a
tiebreak.

Matching is greedy left-to-right — each needle character takes the next
occurrence, not the best one. That is a real limitation, stated in
`fuzzy_match.h` rather than hidden: a query whose letters appear early
in a bad spot and again later in a good one is scored on the early one.
The alternative is a dynamic-programming pass over every candidate on
every keystroke, and this editor's whole claim is that it doesn't do
that sort of thing.

Unlike `applyFilter()`, which hides rows, quick open **rebuilds** the
list in score order — hiding rows in a fixed alphabetical order cannot
put the file you meant first, which is the only thing a fuzzy finder is
for. The sort is stable, so equal scores keep the walk's alphabetical
order instead of shuffling under you as you type, and only the top 200
rows are built: you never look past the first handful, and constructing
thousands of list items per keystroke is exactly the per-frame work
ADR 0053 was about.

### The field is a query, never a path

In Open and Save-As, typing something that looks like a path resolves it
(ADR 0055). In quick open it does not: `src/ed` is something to match
against, not a file to create. Enter opens whatever is highlighted, and
nothing else.

## Consequences

Verified live in this repository: `Ctrl+P` lists 298 files with the
root shown as `~/Lab/area56/textEditor — 298 files`; typing `edvim`
puts `gui/src/editor_viewport_vim.cpp` first, ahead of
`editor_viewport_commands.cpp` and the vim-related ADRs; Enter opens it
as a new buffer; `Down` moves the highlight; `Escape` closes. Opened on
a file outside any repository, the root falls back to that file's own
directory.

The listing header says how many files are in play, and says `first N
files` when the walk hit its cap — a listing that silently stops is a
listing that lies about what you can open.

**The cost, measured rather than assumed.** The walk runs on the UI
thread, once per `Ctrl+P`. Pruned, this repository is ~8ms — instant.
A home directory with no repository above it reaches the 20,000-file cap
in ~0.5s, which is a perceptible hitch. That is the honest ceiling of
this design, and the fixes when it matters are a background thread or
caching the listing between invocations; neither is worth doing before
the hitch is actually in someone's way.

Deferred, and cheap on top of this:

- **Ranking recently-opened files first.** The single biggest quality
  jump a quick-open gets, and it needs a buffer-history list the editor
  doesn't keep yet.
- **Highlighting the matched characters** in each row. The scorer
  already knows the positions; the list renders plain strings.
- **Project-wide search** reuses all of this — the walk, the panel, the
  results list — which is why it is the next thing on the list.
