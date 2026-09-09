# Feature modules

Pluggable modules that sit above the core engine (`../core`) and below/beside
the GUI shell (`../gui`), per the architecture in `docs/SPEC.md` section 3.
None of these have code yet — each subdirectory is a placeholder for the
phase noted below (see `docs/ROADMAP.md`).

- `syntax/` — Tree-sitter integration (Phase 3).
- `lsp/` — LSP client, isolated via JSON-RPC over stdio so a misbehaving
  language server can never crash the editor (Phase 6).
- `plugins/` — Lua scripting host + stable C ABI for compiled plugins
  (Phase 5). Install-by-file only in v1 — no marketplace/registry
  infrastructure (see `docs/SPEC.md` section 6).
