#include "ase/lsp_client.h"

#include "ase/process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <errno.h>
#include <time.h>
#endif

typedef struct {
    int id;
    AseLspResultCallback callback;
    void *user_data;
} PendingRequest;

struct AseLspClient {
    AseProcess *process;
    bool alive;
    /* The initialize reply has arrived and `initialized` has gone out.
     * Until then every other message is queued, not sent: a server may
     * ignore or reject anything before the handshake completes. */
    bool ready;
    long handshake_ms; /* monotonic ms at spawn, for the timeout */
    int next_id;

    /* Framed bodies waiting for `ready`, in order. */
    char **queued;
    size_t queued_count;
    size_t queued_capacity;

    char *read_buffer;
    size_t read_buffer_len;
    size_t read_buffer_cap;

    PendingRequest *pending;
    size_t pending_count;
    size_t pending_capacity;

    AseLspDiagnosticsCallback diagnostics_callback;
    void *diagnostics_user_data;
};

/* -------------------------------------------------------- wait/backoff helper */

/* The only platform-specific bit left in this file — everything else
 * process/pipe-related now lives in ase/process.h (docs/adr/0025),
 * shared with :compile. */
#if defined(_WIN32)

static long monotonic_ms(void) { return 0; }

#else /* POSIX */

static long monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

#endif

/* -------------------------------------------------------- JSON-RPC framing */

static bool append_to_read_buffer(AseLspClient *client, const char *data, size_t len) {
    if (client->read_buffer_len + len > client->read_buffer_cap) {
        size_t new_cap = (client->read_buffer_cap == 0) ? 4096 : client->read_buffer_cap * 2;
        while (new_cap < client->read_buffer_len + len) {
            new_cap *= 2;
        }
        char *grown = (char *)realloc(client->read_buffer, new_cap);
        if (grown == NULL) {
            return false;
        }
        client->read_buffer = grown;
        client->read_buffer_cap = new_cap;
    }
    memcpy(client->read_buffer + client->read_buffer_len, data, len);
    client->read_buffer_len += len;
    return true;
}

static const char *find_crlfcrlf(const char *data, size_t len) {
    if (len < 4) {
        return NULL;
    }
    for (size_t i = 0; i + 4 <= len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            return data + i;
        }
    }
    return NULL;
}

/* Scans \r\n-separated header lines for "Content-Length: N". Returns -1
 * if absent or malformed. */
static long parse_content_length(const char *header, size_t header_len) {
    static const char prefix[] = "Content-Length:";
    size_t prefix_len = sizeof(prefix) - 1;

    size_t pos = 0;
    while (pos < header_len) {
        size_t line_end = pos;
        while (line_end < header_len && header[line_end] != '\r') {
            line_end++;
        }

        if (line_end - pos >= prefix_len && memcmp(header + pos, prefix, prefix_len) == 0) {
            size_t value_start = pos + prefix_len;
            while (value_start < line_end && header[value_start] == ' ') {
                value_start++;
            }
            size_t value_len = line_end - value_start;
            char buf[32];
            if (value_len >= sizeof(buf)) {
                return -1;
            }
            memcpy(buf, header + value_start, value_len);
            buf[value_len] = '\0';

            char *end;
            long value = strtol(buf, &end, 10);
            if (end == buf || value < 0) {
                return -1;
            }
            return value;
        }

        pos = line_end + 2; /* skip the \r\n this line ended with */
        if (pos > header_len) {
            break;
        }
    }
    return -1;
}

static void dispatch_response(AseLspClient *client, int id, const AseJsonValue *message) {
    for (size_t i = 0; i < client->pending_count; i++) {
        if (client->pending[i].id != id) {
            continue;
        }

        PendingRequest req = client->pending[i];
        client->pending[i] = client->pending[client->pending_count - 1];
        client->pending_count--;

        AseJsonValue *result = ase_json_object_get((AseJsonValue *)message, "result");
        AseJsonValue *error = ase_json_object_get((AseJsonValue *)message, "error");
        const char *error_message = NULL;
        if (error != NULL) {
            error_message = ase_json_get_string(ase_json_object_get(error, "message"));
            if (error_message == NULL) {
                error_message = "unknown error";
            }
        }

        if (req.callback != NULL) {
            if (req.callback != NULL) {
                req.callback(req.user_data, (error != NULL) ? NULL : result, error_message);
            }
        }
        return;
    }
    /* response to an id we no longer track (e.g. long expired) — ignore */
}

static void dispatch_diagnostics(AseLspClient *client, const AseJsonValue *params) {
    if (client->diagnostics_callback == NULL || params == NULL) {
        return;
    }

    const char *uri = ase_json_get_string(ase_json_object_get((AseJsonValue *)params, "uri"));
    if (uri == NULL) {
        return;
    }

    AseJsonValue *items = ase_json_object_get((AseJsonValue *)params, "diagnostics");
    size_t count = ase_json_array_size(items);

    AseLspDiagnostic *diagnostics = NULL;
    if (count > 0) {
        diagnostics = (AseLspDiagnostic *)calloc(count, sizeof(AseLspDiagnostic));
        if (diagnostics == NULL) {
            return;
        }
    }

    for (size_t i = 0; i < count; i++) {
        AseJsonValue *d = ase_json_array_get(items, i);
        AseJsonValue *range = ase_json_object_get(d, "range");
        AseJsonValue *start = ase_json_object_get(range, "start");
        AseJsonValue *end = ase_json_object_get(range, "end");

        diagnostics[i].start.line = (int)ase_json_get_number(ase_json_object_get(start, "line"), 0);
        diagnostics[i].start.character = (int)ase_json_get_number(ase_json_object_get(start, "character"), 0);
        diagnostics[i].end.line = (int)ase_json_get_number(ase_json_object_get(end, "line"), 0);
        diagnostics[i].end.character = (int)ase_json_get_number(ase_json_object_get(end, "character"), 0);
        diagnostics[i].severity = (int)ase_json_get_number(ase_json_object_get(d, "severity"), 0);
        const char *msg = ase_json_get_string(ase_json_object_get(d, "message"));
        diagnostics[i].message = (msg != NULL) ? msg : "";
    }

    client->diagnostics_callback(client->diagnostics_user_data, uri, diagnostics, count);
    free(diagnostics);
}

static void dispatch_message(AseLspClient *client, const AseJsonValue *message) {
    if (ase_json_type((AseJsonValue *)message) != ASE_JSON_OBJECT) {
        return;
    }

    AseJsonValue *id_value = ase_json_object_get((AseJsonValue *)message, "id");
    AseJsonValue *method_value = ase_json_object_get((AseJsonValue *)message, "method");
    const char *method = ase_json_get_string(method_value);

    if (id_value != NULL && method == NULL) {
        /* a response to one of our own requests */
        dispatch_response(client, (int)ase_json_get_number(id_value, -1), message);
        return;
    }

    if (method != NULL && id_value == NULL) {
        /* a server-to-client notification */
        if (strcmp(method, "textDocument/publishDiagnostics") == 0) {
            dispatch_diagnostics(client, ase_json_object_get((AseJsonValue *)message, "params"));
        }
        /* other notification methods (window/logMessage, etc.) are out of
         * scope for v1 — diagnostics, completion, definition, references,
         * document symbols and hover only. */
        return;
    }

    /* method != NULL && id_value != NULL: a server-to-client REQUEST.
     * Not implemented in v1 (we never reply) — see docs/adr/0011. A
     * well-behaved server degrades gracefully (that request just never
     * resolves); this never crashes or hangs the client itself. */
}

static void process_complete_messages(AseLspClient *client) {
    for (;;) {
        const char *sep = find_crlfcrlf(client->read_buffer, client->read_buffer_len);
        if (sep == NULL) {
            return;
        }

        size_t header_len = (size_t)(sep - client->read_buffer);
        long content_length = parse_content_length(client->read_buffer, header_len);
        if (content_length < 0) {
            client->alive = false; /* can't recover framing reliably */
            return;
        }

        size_t total_needed = header_len + 4 + (size_t)content_length;
        if (client->read_buffer_len < total_needed) {
            return; /* wait for more bytes */
        }

        const char *body = client->read_buffer + header_len + 4;
        AseJsonValue *message = ase_json_parse(body, (size_t)content_length);
        if (message != NULL) {
            dispatch_message(client, message);
            ase_json_destroy(message);
        }
        /* a malformed JSON body is skipped, not fatal — one bad message
         * from an otherwise-working server shouldn't kill the connection. */

        size_t remaining = client->read_buffer_len - total_needed;
        memmove(client->read_buffer, client->read_buffer + total_needed, remaining);
        client->read_buffer_len = remaining;
    }
}

void ase_lsp_client_poll(AseLspClient *client) {
    if (client == NULL || !client->alive) {
        return;
    }

    /* The handshake is no longer awaited, so its timeout lives here.
     * A server that never answers initialize — a missing binary exits
     * and is caught by EOF below, a hung one is not — is dead to us. */
    if (!client->ready && monotonic_ms() - client->handshake_ms > 3000) {
        client->alive = false;
        return;
    }

    for (;;) {
        char chunk[4096];
        long n = ase_process_read(client->process, chunk, sizeof(chunk));

        if (n > 0) {
            if (!append_to_read_buffer(client, chunk, (size_t)n)) {
                client->alive = false;
                return;
            }
            continue;
        }
        if (n == 0) {
            client->alive = false; /* EOF: server closed its stdout */
            break;
        }

#if !defined(_WIN32)
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
#endif
        client->alive = false; /* any other read error: connection is broken */
        break;
    }

    process_complete_messages(client);
}

/* ---------------------------------------------------------- sending messages */

static bool register_pending(AseLspClient *client, int id, AseLspResultCallback callback, void *user_data) {
    if (client->pending_count == client->pending_capacity) {
        size_t new_cap = (client->pending_capacity == 0) ? 8 : client->pending_capacity * 2;
        PendingRequest *grown = (PendingRequest *)realloc(client->pending, new_cap * sizeof(PendingRequest));
        if (grown == NULL) {
            return false;
        }
        client->pending = grown;
        client->pending_capacity = new_cap;
    }
    client->pending[client->pending_count].id = id;
    client->pending[client->pending_count].callback = callback;
    client->pending[client->pending_count].user_data = user_data;
    client->pending_count++;
    return true;
}

/* Destroys `message` either way. */
/* Takes ownership of `body` either way. */
static bool write_framed(AseLspClient *client, char *body) {
    char header[64];
    int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", strlen(body));

    bool ok = header_len > 0 && (size_t)header_len < sizeof(header) &&
              ase_process_write(client->process, header, (size_t)header_len) &&
              ase_process_write(client->process, body, strlen(body));
    free(body);

    if (!ok) {
        client->alive = false;
    }
    return ok;
}

/* Takes ownership of `body`. */
static bool queue_body(AseLspClient *client, char *body) {
    if (client->queued_count == client->queued_capacity) {
        size_t cap = client->queued_capacity == 0 ? 8 : client->queued_capacity * 2;
        char **grown = (char **)realloc(client->queued, cap * sizeof(char *));
        if (grown == NULL) {
            free(body);
            return false;
        }
        client->queued = grown;
        client->queued_capacity = cap;
    }
    client->queued[client->queued_count++] = body;
    return true;
}

static void flush_queue(AseLspClient *client) {
    for (size_t i = 0; i < client->queued_count; i++) {
        write_framed(client, client->queued[i]);
    }
    client->queued_count = 0;
}

static void drop_queue(AseLspClient *client) {
    for (size_t i = 0; i < client->queued_count; i++) {
        free(client->queued[i]);
    }
    free(client->queued);
    client->queued = NULL;
    client->queued_count = 0;
    client->queued_capacity = 0;
}

/* `immediate` is the handshake's own escape hatch: initialize and the
 * initialized that answers it must go out before `ready` is set, and
 * shutdown goes out on a client that may never have got there. */
static bool send_message_ex(AseLspClient *client, AseJsonValue *message, bool immediate) {
    char *body = ase_json_write(message);
    ase_json_destroy(message);
    if (body == NULL) {
        return false;
    }
    if (!immediate && !client->ready) {
        return queue_body(client, body);
    }
    return write_framed(client, body);
}

static bool send_message(AseLspClient *client, AseJsonValue *message) {
    return send_message_ex(client, message, false);
}

static AseJsonValue *make_request(AseLspClient *client, const char *method, AseJsonValue *params, int *out_id) {
    AseJsonValue *msg = ase_json_object();
    int id = client->next_id++;
    ase_json_object_set(msg, "jsonrpc", ase_json_string("2.0"));
    ase_json_object_set(msg, "id", ase_json_number(id));
    ase_json_object_set(msg, "method", ase_json_string(method));
    if (params != NULL) {
        ase_json_object_set(msg, "params", params);
    }
    *out_id = id;
    return msg;
}

static AseJsonValue *make_notification(const char *method, AseJsonValue *params) {
    AseJsonValue *msg = ase_json_object();
    ase_json_object_set(msg, "jsonrpc", ase_json_string("2.0"));
    ase_json_object_set(msg, "method", ase_json_string(method));
    if (params != NULL) {
        ase_json_object_set(msg, "params", params);
    }
    return msg;
}

static bool send_request_ex(AseLspClient *client, const char *method, AseJsonValue *params,
                             AseLspResultCallback callback, void *user_data, int *out_id,
                             bool immediate) {
    int id;
    AseJsonValue *request = make_request(client, method, params, &id);
    if (!register_pending(client, id, callback, user_data)) {
        ase_json_destroy(request);
        return false;
    }
    if (!send_message_ex(client, request, immediate)) {
        client->pending_count--; /* the entry we just appended never went out */
        return false;
    }
    if (out_id != NULL) {
        *out_id = id;
    }
    return true;
}

static bool send_request(AseLspClient *client, const char *method, AseJsonValue *params,
                          AseLspResultCallback callback, void *user_data, int *out_id) {
    return send_request_ex(client, method, params, callback, user_data, out_id, false);
}

static AseJsonValue *make_position(AseLspPosition pos) {
    AseJsonValue *obj = ase_json_object();
    ase_json_object_set(obj, "line", ase_json_number(pos.line));
    ase_json_object_set(obj, "character", ase_json_number(pos.character));
    return obj;
}

static AseJsonValue *make_text_document_position_params(const char *uri, AseLspPosition position) {
    AseJsonValue *text_document = ase_json_object();
    ase_json_object_set(text_document, "uri", ase_json_string(uri));

    AseJsonValue *params = ase_json_object();
    ase_json_object_set(params, "textDocument", text_document);
    ase_json_object_set(params, "position", make_position(position));
    return params;
}

/* ----------------------------------------------------- handshake / lifecycle */


/* The handshake's second half: answer the server's initialize reply and
 * let everything that queued up behind it go. */
static void initialize_callback(void *user_data, const AseJsonValue *result,
                                 const char *error_message) {
    (void)result;
    AseLspClient *client = (AseLspClient *)user_data;
    if (error_message != NULL) {
        client->alive = false;
        return;
    }
    send_message_ex(client, make_notification("initialized", ase_json_object()), true);
    client->ready = true;
    flush_queue(client);
}

AseLspClient *ase_lsp_client_start(const char *const *command, const char *root_uri) {
    if (command == NULL || command[0] == NULL) {
        return NULL;
    }

    /* LSP servers run in the caller's own working directory — no cwd
     * override needed (unlike :compile, which sets one explicitly).
     * merge_stderr=false: a real server's stderr logging must not land
     * in the same pipe as the framed JSON-RPC stdout stream — see
     * ase_process_spawn_ex's doc comment and docs/adr/0029. */
    AseProcess *process = ase_process_spawn_ex(command, NULL, false);
    if (process == NULL) {
        return NULL;
    }

    AseLspClient *client = (AseLspClient *)calloc(1, sizeof(AseLspClient));
    if (client == NULL) {
        ase_process_destroy(process);
        return NULL;
    }

    client->process = process;
    client->alive = true;
    client->next_id = 1;
    client->handshake_ms = monotonic_ms();

    AseJsonValue *capabilities = ase_json_object(); /* deliberately empty for v1 */
    AseJsonValue *params = ase_json_object();
    ase_json_object_set(params, "processId", ase_json_null());
    ase_json_object_set(params, "rootUri", (root_uri != NULL) ? ase_json_string(root_uri) : ase_json_null());
    ase_json_object_set(params, "capabilities", capabilities);

    /* Sent, not awaited. The reply is picked up by ase_lsp_client_poll,
     * which then sends `initialized` and releases everything queued
     * meanwhile. Waiting here blocked the UI thread for as long as the
     * server took to start — see docs/adr/0093. */
    if (!send_request_ex(client, "initialize", params, initialize_callback, client, NULL, true)) {
        ase_process_destroy(client->process);
        free(client->pending);
        free(client->read_buffer);
        free(client);
        return NULL;
    }

    return client;
}

void ase_lsp_client_stop(AseLspClient *client) {
    if (client == NULL) {
        return;
    }

    if (client->alive) {
        /* Both sent, neither awaited. The spec has a client wait for the
         * shutdown reply before sending exit, but a client that is going
         * away has nothing to do with the answer — and waiting for it
         * blocks the UI thread on a server that may be mid-index. That
         * cost 241ms when a file was closed shortly after opening,
         * against 19ms for an idle server. Closing the pipes below makes
         * the server exit regardless; ase_process_destroy polls for that
         * and kills it only if it does not. See docs/adr/0092. */
        send_request_ex(client, "shutdown", NULL, NULL, NULL, NULL, true);
        send_message_ex(client, make_notification("exit", NULL), true);
    }

    /* Nobody wants a discarded server's exit status, and waiting for it
     * blocked the UI thread. See docs/adr/0092. */
    ase_process_destroy_detached(client->process);

    drop_queue(client);
    free(client->read_buffer);
    free(client->pending);
    free(client);
}

void ase_lsp_client_set_diagnostics_callback(AseLspClient *client, AseLspDiagnosticsCallback callback,
                                              void *user_data) {
    if (client == NULL) {
        return;
    }
    client->diagnostics_callback = callback;
    client->diagnostics_user_data = user_data;
}

bool ase_lsp_client_did_open(AseLspClient *client, const char *uri, const char *language_id, const char *text) {
    if (client == NULL || !client->alive) {
        return false;
    }

    AseJsonValue *text_document = ase_json_object();
    ase_json_object_set(text_document, "uri", ase_json_string(uri));
    ase_json_object_set(text_document, "languageId", ase_json_string(language_id));
    ase_json_object_set(text_document, "version", ase_json_number(1));
    ase_json_object_set(text_document, "text", ase_json_string(text));

    AseJsonValue *params = ase_json_object();
    ase_json_object_set(params, "textDocument", text_document);

    return send_message(client, make_notification("textDocument/didOpen", params));
}

bool ase_lsp_client_did_close(AseLspClient *client, const char *uri) {
    if (client == NULL || !client->alive) {
        return false;
    }

    AseJsonValue *text_document = ase_json_object();
    ase_json_object_set(text_document, "uri", ase_json_string(uri));

    AseJsonValue *params = ase_json_object();
    ase_json_object_set(params, "textDocument", text_document);

    return send_message(client, make_notification("textDocument/didClose", params));
}

bool ase_lsp_client_did_change(AseLspClient *client, const char *uri, int version, const char *text) {
    if (client == NULL || !client->alive) {
        return false;
    }

    AseJsonValue *text_document = ase_json_object();
    ase_json_object_set(text_document, "uri", ase_json_string(uri));
    ase_json_object_set(text_document, "version", ase_json_number(version));

    /* No "range" field — a whole-document replacement (TextDocumentSyncKind.Full),
     * not an incremental change. */
    AseJsonValue *change = ase_json_object();
    ase_json_object_set(change, "text", ase_json_string(text));
    AseJsonValue *content_changes = ase_json_array();
    ase_json_array_append(content_changes, change);

    AseJsonValue *params = ase_json_object();
    ase_json_object_set(params, "textDocument", text_document);
    ase_json_object_set(params, "contentChanges", content_changes);

    return send_message(client, make_notification("textDocument/didChange", params));
}

bool ase_lsp_client_request_completion(AseLspClient *client, const char *uri, AseLspPosition position,
                                        AseLspResultCallback callback, void *user_data) {
    if (client == NULL || !client->alive) {
        return false;
    }
    AseJsonValue *params = make_text_document_position_params(uri, position);
    return send_request(client, "textDocument/completion", params, callback, user_data, NULL);
}

bool ase_lsp_client_request_definition(AseLspClient *client, const char *uri, AseLspPosition position,
                                        AseLspResultCallback callback, void *user_data) {
    if (client == NULL || !client->alive) {
        return false;
    }
    AseJsonValue *params = make_text_document_position_params(uri, position);
    return send_request(client, "textDocument/definition", params, callback, user_data, NULL);
}

/* Unlike definition, this one carries a context object: without
 * includeDeclaration a server may or may not list the declaration
 * itself, and "who uses this" reads better with it than without. */
bool ase_lsp_client_request_references(AseLspClient *client, const char *uri, AseLspPosition position,
                                        bool include_declaration, AseLspResultCallback callback,
                                        void *user_data) {
    if (client == NULL || !client->alive) {
        return false;
    }
    AseJsonValue *params = make_text_document_position_params(uri, position);
    AseJsonValue *context = ase_json_object();
    ase_json_object_set(context, "includeDeclaration", ase_json_bool(include_declaration));
    ase_json_object_set(params, "context", context);
    return send_request(client, "textDocument/references", params, callback, user_data, NULL);
}

/* No position: this one is about the whole file. */
bool ase_lsp_client_request_document_symbols(AseLspClient *client, const char *uri,
                                              AseLspResultCallback callback, void *user_data) {
    if (client == NULL || !client->alive) {
        return false;
    }
    AseJsonValue *text_document = ase_json_object();
    ase_json_object_set(text_document, "uri", ase_json_string(uri));
    AseJsonValue *params = ase_json_object();
    ase_json_object_set(params, "textDocument", text_document);
    return send_request(client, "textDocument/documentSymbol", params, callback, user_data, NULL);
}

bool ase_lsp_client_request_hover(AseLspClient *client, const char *uri, AseLspPosition position,
                                   AseLspResultCallback callback, void *user_data) {
    if (client == NULL || !client->alive) {
        return false;
    }
    AseJsonValue *params = make_text_document_position_params(uri, position);
    return send_request(client, "textDocument/hover", params, callback, user_data, NULL);
}

bool ase_lsp_client_is_ready(const AseLspClient *client) {
    return client != NULL && client->alive && client->ready;
}

bool ase_lsp_client_is_alive(const AseLspClient *client) {
    return client != NULL && client->alive;
}
