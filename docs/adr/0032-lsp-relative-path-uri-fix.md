# ADR 0032: Fix LSP URI construction for relative paths

## Status

Accepted

## Context

User-reported: after enabling `lsp_command = clangd`, launching the
editor as `ase_gui file.c` (a relative path — the common case from a
shell already `cd`'d into the project) produced no diagnostics,
completion, or hover at all. `clangd`'s own log showed every message
rejected:

```
E[...] Failed to decode textDocument/didOpen request: unresolvable URI at (root).textDocument.uri
```

## Decision

`startLspClientIfConfigured()` built the document URI with
`QUrl::fromLocalFile(m_filePath).toString()`, where `m_filePath` is
whatever was passed on the command line — verbatim, never resolved
against the working directory. `QUrl::fromLocalFile` expects an
*absolute* path; given a relative one, it produces a malformed URI
that a real server's strict URI parser rejects outright (LSP requires
well-formed `file:///...` URIs). This didn't surface in Phase 16/17's
own live verification because every test there happened to launch with
an already-absolute scratch path.

Fixed by resolving through `QFileInfo(m_filePath).absoluteFilePath()`
before building the URI — resolves a relative path against the
current working directory, exactly as the shell itself did to find the
file in the first place. `m_filePath` itself is left untouched (still
used as-is for the save target, window title, etc.); only the LSP URI
construction changed.

## Consequences

Verified live: launched `ase_gui file.c` from within the file's own
directory (reproducing the report exactly, including the process's
actual working directory), confirmed `clangd`'s log now shows a clean
`didOpen` → `publishDiagnostics` round trip with no decode error, and
that typing a partial identifier produces a real completion popup.
Full test suite (9/9) still passes — this is a GUI-only, single-line
fix, no core changes.
