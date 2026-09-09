# Feature modules

Pluggable modules that sit above the core engine (`../core`) and below/beside
the GUI shell (`../gui`), per the architecture in `docs/SPEC.md` section 3.

- `syntax/` — Tree-sitter integration (Phase 3, done).

That's the only one. Both the LSP client (Phase 6) and the plugin ABI +
Lua scripting host (Phase 5) ended up in `../core` instead, not here —
the architecture diagram in `docs/SPEC.md` section 3 places both under
Editor Core, and unlike Tree-sitter, neither has a GUI dependency that
would motivate isolating it in `modules/`. See
`../core/include/ase/lsp_client.h` + `../docs/adr/0011-lsp-client.md`,
and `../core/include/ase/plugin_host.h` +
`../docs/adr/0009-plugin-abi-and-lua-host.md`. (An earlier version of
this README carried `plugins/` and `lsp/` placeholders here before
those phases were designed — corrected once their real locations were
settled, rather than left stale.)
