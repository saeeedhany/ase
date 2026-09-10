#ifndef ASE_LSP_CLIENT_H
#define ASE_LSP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#include "ase/json.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LSP client: spawns a language server as a child process and speaks
 * JSON-RPC over its stdin/stdout, isolated in its own process so a
 * misbehaving server can't crash the editor (spec section 4). Covers
 * diagnostics, completion, and go-to-definition — see
 * docs/adr/0011-lsp-client.md for the process-isolation model, the
 * synchronous-handshake choice, and why this is POSIX-only in v1.
 */

typedef struct AseLspClient AseLspClient;

typedef struct {
    int line;      /* 0-based, per LSP */
    int character; /* 0-based UTF-16 code unit offset, per LSP */
} AseLspPosition;

typedef struct {
    AseLspPosition start;
    AseLspPosition end;
    int severity;         /* 1=Error, 2=Warning, 3=Information, 4=Hint; 0 = unspecified */
    const char *message;  /* borrowed — valid only during the callback */
} AseLspDiagnostic;

typedef void (*AseLspDiagnosticsCallback)(void *user_data, const char *uri,
                                           const AseLspDiagnostic *diagnostics, size_t count);

/* Result of a completion/definition request. `result` is the raw JSON
 * `result` field (borrowed — valid only during the callback). If the
 * server responded with an error instead, `result` is NULL and
 * `error_message` is set (also borrowed); if there was no error,
 * `error_message` is NULL. */
typedef void (*AseLspResultCallback)(void *user_data, const AseJsonValue *result,
                                      const char *error_message);

/* Spawns `command` (a NULL-terminated argv-style array — command[0] is
 * the executable) and performs the initialize/initialized handshake,
 * blocking for up to a few seconds for the server's response. Returns
 * NULL if the process couldn't be spawned, the handshake didn't
 * complete in time, or (v1) this platform doesn't support it yet — see
 * ase_lsp_client_is_alive being meaningless on a NULL client; NULL
 * always means "no client," never a client to introspect further.
 * `root_uri` may be NULL. */
AseLspClient *ase_lsp_client_start(const char *const *command, const char *root_uri);

/* Attempts a clean shutdown/exit handshake (bounded wait), then
 * force-terminates the process if it hasn't exited, then frees
 * everything. Safe to call even if the server already died. */
void ase_lsp_client_stop(AseLspClient *client);

void ase_lsp_client_set_diagnostics_callback(AseLspClient *client,
                                              AseLspDiagnosticsCallback callback, void *user_data);

/* textDocument/didOpen notification — fire-and-forget, no response. */
bool ase_lsp_client_did_open(AseLspClient *client, const char *uri,
                              const char *language_id, const char *text);

/* textDocument/didChange notification — full-document sync (the whole
 * new `text`, not an incremental range), matching this project's
 * existing "full-buffer mirroring" philosophy (docs/adr/0006) rather
 * than tracking incremental edit ranges. `version` must increase on
 * every call for the same document (per the LSP spec); the caller owns
 * that counter. Fire-and-forget, no response — see docs/adr/0029. */
bool ase_lsp_client_did_change(AseLspClient *client, const char *uri, int version, const char *text);

/* Async: the request is sent immediately, but `callback` only fires
 * later, from inside ase_lsp_client_poll(), once the response arrives.
 * Returns false immediately (callback never fires) if the client is
 * dead or the request couldn't be sent. */
bool ase_lsp_client_request_completion(AseLspClient *client, const char *uri, AseLspPosition position,
                                        AseLspResultCallback callback, void *user_data);
bool ase_lsp_client_request_definition(AseLspClient *client, const char *uri, AseLspPosition position,
                                        AseLspResultCallback callback, void *user_data);
/* textDocument/hover — result's shape varies by server (a plain
 * string, a MarkedString, a MarkedString[], or MarkupContent), so it's
 * handed back as raw JSON like completion/definition rather than
 * parsed here; the caller picks apart whichever shape shows up. See
 * docs/adr/0030. */
bool ase_lsp_client_request_hover(AseLspClient *client, const char *uri, AseLspPosition position,
                                   AseLspResultCallback callback, void *user_data);

/* Non-blocking: drains whatever the server has written so far and
 * dispatches any complete messages (diagnostics notifications, pending
 * request responses). Never blocks waiting for the server — call this
 * periodically (e.g. a GUI timer), the same shape as config hot-reload
 * polling. A dead client makes this a safe no-op. */
void ase_lsp_client_poll(AseLspClient *client);

/* False once the server process has exited/crashed or a fatal framing
 * error occurred. The client is otherwise inert past that point —
 * poll/request calls become no-ops rather than crashing — but you
 * still own calling ase_lsp_client_stop() to free it. */
bool ase_lsp_client_is_alive(const AseLspClient *client);

#ifdef __cplusplus
}
#endif

#endif /* ASE_LSP_CLIENT_H */
