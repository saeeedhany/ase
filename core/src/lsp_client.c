#include "ase/lsp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

typedef struct {
    int id;
    AseLspResultCallback callback;
    void *user_data;
} PendingRequest;

struct AseLspClient {
    long child_pid;
    int read_fd;
    int write_fd;
    bool alive;
    int next_id;

    char *read_buffer;
    size_t read_buffer_len;
    size_t read_buffer_cap;

    PendingRequest *pending;
    size_t pending_count;
    size_t pending_capacity;

    AseLspDiagnosticsCallback diagnostics_callback;
    void *diagnostics_user_data;
};

/* -------------------------------------------- platform process/pipe primitives */

#if defined(_WIN32)

/* Not implemented: async child-process I/O on Windows needs overlapped
 * I/O or a reader thread, neither of which this phase attempts — see
 * docs/adr/0011-lsp-client.md. ase_lsp_client_start() returns NULL
 * cleanly instead of spawning anything; every function below this
 * point is consequently unreachable on Windows (every public entry
 * point checks for a NULL/dead client first) but must still exist to
 * link. */

static bool platform_spawn(const char *const *command, long *out_pid, int *out_read_fd, int *out_write_fd) {
    (void)command;
    (void)out_pid;
    (void)out_read_fd;
    (void)out_write_fd;
    return false;
}

static long platform_read_nonblocking(int fd, char *buf, size_t cap) {
    (void)fd;
    (void)buf;
    (void)cap;
    return -1;
}

static bool platform_write_all(int fd, const char *data, size_t len) {
    (void)fd;
    (void)data;
    (void)len;
    return false;
}

static void platform_terminate(long pid, int read_fd, int write_fd) {
    (void)pid;
    (void)read_fd;
    (void)write_fd;
}

static void sleep_ms(int ms) {
    (void)ms;
}

#else /* POSIX */

static bool platform_spawn(const char *const *command, long *out_pid, int *out_read_fd, int *out_write_fd) {
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) != 0) {
        return false;
    }
    if (pipe(stdout_pipe) != 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return false;
    }

    if (pid == 0) {
        /* child */
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        execvp(command[0], (char *const *)command);
        _exit(127); /* execvp failed */
    }

    /* parent */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    *out_pid = (long)pid;
    *out_write_fd = stdin_pipe[1];
    *out_read_fd = stdout_pipe[0];
    return true;
}

static long platform_read_nonblocking(int fd, char *buf, size_t cap) {
    return (long)read(fd, buf, cap);
}

static bool platform_write_all(int fd, const char *data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n > 0) {
            written += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false; /* EPIPE (server died) or any other error */
    }
    return true;
}

static void platform_terminate(long pid, int read_fd, int write_fd) {
    if (write_fd >= 0) {
        close(write_fd);
    }
    if (read_fd >= 0) {
        close(read_fd);
    }

    if (pid > 0) {
        pid_t p = (pid_t)pid;
        int status;
        if (waitpid(p, &status, WNOHANG) == 0) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 200L * 1000000L;
            nanosleep(&ts, NULL);
            if (waitpid(p, &status, WNOHANG) == 0) {
                kill(p, SIGKILL);
                waitpid(p, &status, 0);
            }
        }
    }
}

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
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
            req.callback(req.user_data, (error != NULL) ? NULL : result, error_message);
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
         * scope for v1 — diagnostics/completion/definition only. */
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

    for (;;) {
        char chunk[4096];
        long n = platform_read_nonblocking(client->read_fd, chunk, sizeof(chunk));

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
static bool send_message(AseLspClient *client, AseJsonValue *message) {
    char *body = ase_json_write(message);
    ase_json_destroy(message);
    if (body == NULL) {
        return false;
    }

    char header[64];
    int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", strlen(body));

    bool ok = header_len > 0 && (size_t)header_len < sizeof(header) &&
              platform_write_all(client->write_fd, header, (size_t)header_len) &&
              platform_write_all(client->write_fd, body, strlen(body));
    free(body);

    if (!ok) {
        client->alive = false;
    }
    return ok;
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

static bool send_request(AseLspClient *client, const char *method, AseJsonValue *params,
                          AseLspResultCallback callback, void *user_data, int *out_id) {
    int id;
    AseJsonValue *request = make_request(client, method, params, &id);
    if (!register_pending(client, id, callback, user_data)) {
        ase_json_destroy(request);
        return false;
    }
    if (!send_message(client, request)) {
        client->pending_count--; /* the entry we just appended never went out */
        return false;
    }
    if (out_id != NULL) {
        *out_id = id;
    }
    return true;
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

typedef struct {
    bool done;
    bool success;
} WaitState;

static void wait_state_callback(void *user_data, const AseJsonValue *result, const char *error_message) {
    (void)result;
    WaitState *state = (WaitState *)user_data;
    state->done = true;
    state->success = (error_message == NULL);
}

/* Polls until `state->done`, the client dies, or `timeout_ms` elapses. */
static void wait_for(AseLspClient *client, WaitState *state, int timeout_ms) {
    int elapsed = 0;
    const int step_ms = 10;
    while (!state->done && client->alive && elapsed < timeout_ms) {
        ase_lsp_client_poll(client);
        if (state->done) {
            break;
        }
        sleep_ms(step_ms);
        elapsed += step_ms;
    }
}

AseLspClient *ase_lsp_client_start(const char *const *command, const char *root_uri) {
    if (command == NULL || command[0] == NULL) {
        return NULL;
    }

#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN); /* see docs/adr/0011 */
#endif

    long pid;
    int read_fd;
    int write_fd;
    if (!platform_spawn(command, &pid, &read_fd, &write_fd)) {
        return NULL;
    }

    AseLspClient *client = (AseLspClient *)calloc(1, sizeof(AseLspClient));
    if (client == NULL) {
        platform_terminate(pid, read_fd, write_fd);
        return NULL;
    }

    client->child_pid = pid;
    client->read_fd = read_fd;
    client->write_fd = write_fd;
    client->alive = true;
    client->next_id = 1;

    AseJsonValue *capabilities = ase_json_object(); /* deliberately empty for v1 */
    AseJsonValue *params = ase_json_object();
    ase_json_object_set(params, "processId", ase_json_null());
    ase_json_object_set(params, "rootUri", (root_uri != NULL) ? ase_json_string(root_uri) : ase_json_null());
    ase_json_object_set(params, "capabilities", capabilities);

    WaitState state = {false, false};
    if (!send_request(client, "initialize", params, wait_state_callback, &state, NULL)) {
        platform_terminate(client->child_pid, client->read_fd, client->write_fd);
        free(client->pending);
        free(client->read_buffer);
        free(client);
        return NULL;
    }

    wait_for(client, &state, 3000);

    if (!state.done || !state.success) {
        ase_lsp_client_stop(client);
        return NULL;
    }

    send_message(client, make_notification("initialized", ase_json_object()));
    return client;
}

void ase_lsp_client_stop(AseLspClient *client) {
    if (client == NULL) {
        return;
    }

    if (client->alive) {
        WaitState state = {false, false};
        if (send_request(client, "shutdown", NULL, wait_state_callback, &state, NULL)) {
            wait_for(client, &state, 1000);
        }
        if (client->alive) {
            send_message(client, make_notification("exit", NULL));
        }
    }

    platform_terminate(client->child_pid, client->read_fd, client->write_fd);

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

bool ase_lsp_client_is_alive(const AseLspClient *client) {
    return client != NULL && client->alive;
}
