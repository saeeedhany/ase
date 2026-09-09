# Feature modules

Pluggable modules that sit above the core engine (`../core`) and below/beside
the GUI shell (`../gui`), per the architecture in `docs/SPEC.md` section 3.

- `syntax/` — Tree-sitter integration (Phase 3, done).
- `lsp/` — LSP client, isolated via JSON-RPC over stdio so a misbehaving
  language server can never crash the editor (Phase 6, not started).

The plugin ABI + Lua scripting host (Phase 5) is **not** here — the
architecture diagram in `docs/SPEC.md` section 3 places it in Editor
Core, not Feature Modules, and unlike Tree-sitter/LSP it has no external
process or GUI dependency that would motivate isolating it. See
`../core/include/ase/plugin_host.h` and
`../docs/adr/0009-plugin-abi-and-lua-host.md`. (An earlier version of
this README put a `plugins/` placeholder here before that was
implemented — corrected once the real location was settled.)
