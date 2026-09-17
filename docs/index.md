# Absolute Simple Editor

A minimal, robust, blazingly fast, and aesthetically deliberate GUI text
editor.

[:octicons-download-16: Install](guide/install.md){ .md-button .md-button--primary }
[:octicons-rocket-16: First five minutes](guide/getting-started.md){ .md-button }
[:octicons-mark-github-16: Source](https://github.com/saeeedhany/ase){ .md-button }

Core and GUI are cleanly separated — the editing engine is a headless C
library (`core/`) with a stable C ABI, and the GUI (`gui/`) is a
replaceable Qt6 shell on top of it. See
[ADR 0002](adr/0002-headless-core-separation.md) for why.

## Where to start

<div class="grid cards" markdown>

- **[Guide](guide/install.md)**

    Install it, configure it, rebind it, extend it. Start with
    [your first five minutes](guide/getting-started.md).

- **[Reference](reference/index.md)**

    Every command, every default binding, every config key — generated
    from the source, so they cannot drift from the binary.

- **[Decisions](adr/index.md)**

    Every architecturally significant choice, in the order it was made,
    with the reasoning and what it cost.

- **[Roadmap](ROADMAP.md)**

    What is done, phase by phase, and what is still open.

</div>

## Status

**`v0.3.0-alpha`**, licensed under the
[Apache License 2.0](https://github.com/saeeedhany/ase/blob/main/LICENSE).

All seven original spec phases, the "complete normal editor" pass, and a
Vim mode well past its original scope — motions, operators with counts,
text objects, marks, macros, named registers, `.` repeat, Visual and
Replace modes, `/` search and `:s`, pinned by 218 conformance cases
generated from real vim.

Tree-sitter highlighting for seven languages, an LSP client with
diagnostics, completion, hover, go-to-definition, find-references and a
document outline, three built-in palettes, keybindings as data, session
restore, git gutter marks, and a save path that cannot lose the file it
is writing.

Linux is the supported platform — see
[Which platforms](guide/install.md#which-platforms). On Windows the
language server and `:compile` are present and do nothing.

See the [roadmap](ROADMAP.md) for what is still open, and
[Contributing](https://github.com/saeeedhany/ase/blob/main/CONTRIBUTING.md)
for the ground rules.
