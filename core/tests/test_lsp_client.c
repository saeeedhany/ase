#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/lsp_client.h"

#ifndef ASE_TEST_FAKE_LSP_SERVER
#error "ASE_TEST_FAKE_LSP_SERVER must be defined by CMake to the fake server's built path"
#endif

typedef struct {
    bool called;
    char uri[256];
    size_t count;
    AseLspDiagnostic first;
    char first_message[256];
} DiagnosticsCapture;

static void on_diagnostics(void *user_data, const char *uri, const AseLspDiagnostic *diagnostics, size_t count) {
    DiagnosticsCapture *capture = (DiagnosticsCapture *)user_data;
    capture->called = true;
    snprintf(capture->uri, sizeof(capture->uri), "%s", uri);
    capture->count = count;
    if (count > 0) {
        capture->first = diagnostics[0];
        snprintf(capture->first_message, sizeof(capture->first_message), "%s", diagnostics[0].message);
    }
}

typedef struct {
    bool called;
    bool had_error;
    char result_text[1024];
} ResultCapture;

static void on_result(void *user_data, const AseJsonValue *result, const char *error_message) {
    ResultCapture *capture = (ResultCapture *)user_data;
    capture->called = true;
    capture->had_error = (error_message != NULL);
    if (result != NULL) {
        char *text = ase_json_write(result);
        if (text != NULL) {
            snprintf(capture->result_text, sizeof(capture->result_text), "%s", text);
            free(text);
        }
    }
}

/* Iteration-bounded, not time-bounded — keeps this test free of any
 * platform-specific sleep API. Local pipe IPC round-trips well within
 * this many non-blocking poll attempts. */
static void poll_until(AseLspClient *client, const bool *flag, int max_iterations) {
    for (int i = 0; i < max_iterations && !*flag && ase_lsp_client_is_alive(client); i++) {
        ase_lsp_client_poll(client);
    }
}

static void test_missing_server_returns_null(void) {
    /* A misconfigured/uninstalled language server is a common real-world
     * case, not just a theoretical one — must fail cleanly, not hang or
     * crash. */
    const char *command[] = {"/this/path/does/not/exist/ase_fake_lsp", NULL};
    AseLspClient *client = ase_lsp_client_start(command, NULL);
    assert(client == NULL);
}

static void test_full_lifecycle(void) {
    const char *command[] = {ASE_TEST_FAKE_LSP_SERVER, NULL};
    AseLspClient *client = ase_lsp_client_start(command, "file:///workspace");

#if defined(_WIN32)
    /* Documented v1 limitation — see docs/adr/0011, decision 6. */
    assert(client == NULL);
    (void)poll_until;
    (void)on_diagnostics;
    (void)on_result;
#else
    assert(client != NULL);
    assert(ase_lsp_client_is_alive(client));

    DiagnosticsCapture diag_capture;
    memset(&diag_capture, 0, sizeof(diag_capture));
    ase_lsp_client_set_diagnostics_callback(client, on_diagnostics, &diag_capture);

    assert(ase_lsp_client_did_open(client, "file:///fake.txt", "plaintext", "hello world"));
    poll_until(client, &diag_capture.called, 1000000);
    assert(diag_capture.called);
    assert(strcmp(diag_capture.uri, "file:///fake.txt") == 0);
    assert(diag_capture.count == 1);
    assert(strcmp(diag_capture.first_message, "fake diagnostic") == 0);
    assert(diag_capture.first.severity == 1);
    assert(diag_capture.first.end.character == 5);

    /* didChange must actually reach the server and refresh diagnostics
     * — the documented gap this phase exists to close ("results go
     * stale after the first edit"). The fake server replies to
     * didChange with an *empty* diagnostics list, distinguishable from
     * didOpen's one-item reply, so this proves the notification was
     * really sent and really dispatched, not just that some stale
     * callback fired again. */
    diag_capture.called = false;
    assert(ase_lsp_client_did_change(client, "file:///fake.txt", 2, "hello world, fixed"));
    poll_until(client, &diag_capture.called, 1000000);
    assert(diag_capture.called);
    assert(diag_capture.count == 0);

    ResultCapture completion_capture;
    memset(&completion_capture, 0, sizeof(completion_capture));
    AseLspPosition pos;
    pos.line = 0;
    pos.character = 0;
    assert(ase_lsp_client_request_completion(client, "file:///fake.txt", pos, on_result, &completion_capture));
    poll_until(client, &completion_capture.called, 1000000);
    assert(completion_capture.called);
    assert(!completion_capture.had_error);
    assert(strstr(completion_capture.result_text, "fake_completion_item") != NULL);

    ResultCapture definition_capture;
    memset(&definition_capture, 0, sizeof(definition_capture));
    assert(ase_lsp_client_request_definition(client, "file:///fake.txt", pos, on_result, &definition_capture));
    poll_until(client, &definition_capture.called, 1000000);
    assert(definition_capture.called);
    assert(!definition_capture.had_error);
    assert(strstr(definition_capture.result_text, "file:///fake.txt") != NULL);

    ResultCapture hover_capture;
    memset(&hover_capture, 0, sizeof(hover_capture));
    assert(ase_lsp_client_request_hover(client, "file:///fake.txt", pos, on_result, &hover_capture));
    poll_until(client, &hover_capture.called, 1000000);
    assert(hover_capture.called);
    assert(!hover_capture.had_error);
    assert(strstr(hover_capture.result_text, "fake hover text") != NULL);

    assert(ase_lsp_client_is_alive(client));
    ase_lsp_client_stop(client);
#endif
}

int main(void) {
    test_missing_server_returns_null();
    test_full_lifecycle();

    printf("all lsp client tests passed\n");
    return 0;
}
