#ifndef ASE_LSP_CLIENT_H
#define ASE_LSP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#include "ase/json.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Spawns a language server as a child process and speaks JSON-RPC over
 * its stdin/stdout, so a misbehaving server cannot crash the editor.
 * POSIX-only in v1. See docs/adr/0011.
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

/* Both pointers are borrowed, valid only during the callback. On a
 * server error `result` is NULL and `error_message` is set. */
typedef void (*AseLspResultCallback)(void *user_data, const AseJsonValue *result,
                                      const char *error_message);

/* Spawns `command` (NULL-terminated argv) and performs the handshake,
 * blocking a few seconds for the response. NULL means no client, never
 * one to introspect further. `root_uri` may be NULL. */
AseLspClient *ase_lsp_client_start(const char *const *command, const char *root_uri);

/* Clean shutdown handshake, then force-terminate, then free. Safe
 * even if the server already died. */
void ase_lsp_client_stop(AseLspClient *client);

void ase_lsp_client_set_diagnostics_callback(AseLspClient *client,
                                              AseLspDiagnosticsCallback callback, void *user_data);

/* textDocument/didOpen notification — fire-and-forget, no response. */
bool ase_lsp_client_did_open(AseLspClient *client, const char *uri,
                              const char *language_id, const char *text);

/* Full-document sync, not an incremental range. `version` must
 * increase on every call; the caller owns that counter.
 * Fire-and-forget. */
bool ase_lsp_client_did_change(AseLspClient *client, const char *uri, int version, const char *text);

/* Async: `callback` fires later, from ase_lsp_client_poll(). Returns
 * false, and never fires, if the client is dead. */
bool ase_lsp_client_request_completion(AseLspClient *client, const char *uri, AseLspPosition position,
                                        AseLspResultCallback callback, void *user_data);
bool ase_lsp_client_request_definition(AseLspClient *client, const char *uri, AseLspPosition position,
                                        AseLspResultCallback callback, void *user_data);
/* The result's shape varies by server, so it is handed back as raw
 * JSON for the caller to pick apart. */
bool ase_lsp_client_request_hover(AseLspClient *client, const char *uri, AseLspPosition position,
                                   AseLspResultCallback callback, void *user_data);

/* Drains whatever the server has written and dispatches any complete
 * messages. Never blocks; call it periodically. No-op when dead. */
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
