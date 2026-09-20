#include "test_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
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

static void test_missing_server_dies_on_poll(void) {
    /* A misconfigured/uninstalled language server is a common real-world
     * case, not just a theoretical one — must fail cleanly, not hang or
     * crash. Since the handshake is no longer awaited (docs/adr/0093),
     * the spawn itself succeeds and the failure surfaces as the child
     * exiting: poll sees EOF and the client stops being alive. */
    const char *command[] = {"/this/path/does/not/exist/ase_fake_lsp", NULL};
    errno = 0;
    AseLspClient *client = ase_lsp_client_start(command, NULL);
#if defined(_WIN32)
    CHECK(client == NULL);
#else
    /* This has failed on a CI runner and on no machine it could be
     * reproduced on. Spawning only returns NULL when pipe() or fork()
     * does, so say which — a bare "client != NULL" names the symptom
     * and nothing else. See docs/adr/0130. */
    if (client == NULL) {
        /* stderr, and flushed: CHECK aborts, and an abort throws away
         * whatever is sitting in stdout's buffer — which is exactly
         * where the first version of this diagnostic went. */
        fprintf(stderr, "spawn failed: %s\n", errno != 0 ? strerror(errno) : "errno unset");
        fflush(stderr);
    }
    CHECK(client != NULL);
    CHECK(!ase_lsp_client_is_ready(client));
    for (int i = 0; i < 1000000 && ase_lsp_client_is_alive(client); i++) {
        ase_lsp_client_poll(client);
    }
    CHECK(!ase_lsp_client_is_alive(client));
    CHECK(!ase_lsp_client_is_ready(client));
    ase_lsp_client_stop(client);
#endif
}

static void test_full_lifecycle(void) {
    const char *command[] = {ASE_TEST_FAKE_LSP_SERVER, NULL};
    AseLspClient *client = ase_lsp_client_start(command, "file:///workspace");

#if defined(_WIN32)
    /* Documented v1 limitation — see docs/adr/0011, decision 6. */
    CHECK(client == NULL);
    (void)poll_until;
    (void)on_diagnostics;
    (void)on_result;
#else
    CHECK(client != NULL);
    CHECK(ase_lsp_client_is_alive(client));
    /* Not ready yet: the handshake is in flight, and anything sent now
     * queues until it lands. */
    CHECK(!ase_lsp_client_is_ready(client));

    DiagnosticsCapture diag_capture;
    memset(&diag_capture, 0, sizeof(diag_capture));
    ase_lsp_client_set_diagnostics_callback(client, on_diagnostics, &diag_capture);

    CHECK(ase_lsp_client_did_open(client, "file:///fake.txt", "plaintext", "hello world"));
    poll_until(client, &diag_capture.called, 1000000);
    CHECK(diag_capture.called);
    CHECK(strcmp(diag_capture.uri, "file:///fake.txt") == 0);
    CHECK(diag_capture.count == 1);
    CHECK(strcmp(diag_capture.first_message, "fake diagnostic") == 0);
    CHECK(diag_capture.first.severity == 1);
    CHECK(diag_capture.first.end.character == 5);

    /* didChange must actually reach the server and refresh diagnostics
     * — the documented gap this phase exists to close ("results go
     * stale after the first edit"). The fake server replies to
     * didChange with an *empty* diagnostics list, distinguishable from
     * didOpen's one-item reply, so this proves the notification was
     * really sent and really dispatched, not just that some stale
     * callback fired again. */
    diag_capture.called = false;
    CHECK(ase_lsp_client_did_change(client, "file:///fake.txt", 2, "hello world, fixed"));
    poll_until(client, &diag_capture.called, 1000000);
    CHECK(diag_capture.called);
    CHECK(diag_capture.count == 0);

    ResultCapture completion_capture;
    memset(&completion_capture, 0, sizeof(completion_capture));
    AseLspPosition pos;
    pos.line = 0;
    pos.character = 0;
    CHECK(ase_lsp_client_request_completion(client, "file:///fake.txt", pos, on_result, &completion_capture));
    poll_until(client, &completion_capture.called, 1000000);
    CHECK(completion_capture.called);
    CHECK(!completion_capture.had_error);
    CHECK(strstr(completion_capture.result_text, "fake_completion_item") != NULL);

    ResultCapture references_capture;
    memset(&references_capture, 0, sizeof(references_capture));
    CHECK(ase_lsp_client_request_references(client, "file:///fake.txt", pos, true, on_result,
                                             &references_capture));
    poll_until(client, &references_capture.called, 1000000);
    CHECK(references_capture.called);
    CHECK(!references_capture.had_error);
    /* Both locations, not just the first. */
    CHECK(strstr(references_capture.result_text, "file:///fake.txt") != NULL);
    CHECK(strstr(references_capture.result_text, "file:///other.txt") != NULL);

    ResultCapture symbols_capture;
    memset(&symbols_capture, 0, sizeof(symbols_capture));
    CHECK(ase_lsp_client_request_document_symbols(client, "file:///fake.txt", on_result,
                                                   &symbols_capture));
    poll_until(client, &symbols_capture.called, 1000000);
    CHECK(symbols_capture.called);
    CHECK(!symbols_capture.had_error);
    CHECK(strstr(symbols_capture.result_text, "outer_symbol") != NULL);
    /* Nested children survive the round trip too. */
    CHECK(strstr(symbols_capture.result_text, "inner_symbol") != NULL);

    ResultCapture rename_capture;
    memset(&rename_capture, 0, sizeof(rename_capture));
    CHECK(ase_lsp_client_request_rename(client, "file:///fake.txt", pos, "gadget", on_result,
                                         &rename_capture));
    poll_until(client, &rename_capture.called, 1000000);
    CHECK(rename_capture.called);
    CHECK(!rename_capture.had_error);
    /* Both files and both edits in the first one — a reader that stops
     * at the first of either is caught here. */
    CHECK(strstr(rename_capture.result_text, "changes") != NULL);
    CHECK(strstr(rename_capture.result_text, "file:///fake.txt") != NULL);
    CHECK(strstr(rename_capture.result_text, "file:///other.txt") != NULL);
    CHECK(strstr(rename_capture.result_text, "gadget") != NULL);

    /* An empty new name is refused before anything is sent: a rename to
     * nothing is not a rename. */
    ResultCapture unused_capture;
    memset(&unused_capture, 0, sizeof(unused_capture));
    CHECK(!ase_lsp_client_request_rename(client, "file:///fake.txt", pos, "", on_result,
                                          &unused_capture));
    CHECK(!unused_capture.called);

    ResultCapture definition_capture;
    memset(&definition_capture, 0, sizeof(definition_capture));
    CHECK(ase_lsp_client_request_definition(client, "file:///fake.txt", pos, on_result, &definition_capture));
    poll_until(client, &definition_capture.called, 1000000);
    CHECK(definition_capture.called);
    CHECK(!definition_capture.had_error);
    CHECK(strstr(definition_capture.result_text, "file:///fake.txt") != NULL);

    ResultCapture hover_capture;
    memset(&hover_capture, 0, sizeof(hover_capture));
    CHECK(ase_lsp_client_request_hover(client, "file:///fake.txt", pos, on_result, &hover_capture));
    poll_until(client, &hover_capture.called, 1000000);
    CHECK(hover_capture.called);
    CHECK(!hover_capture.had_error);
    CHECK(strstr(hover_capture.result_text, "fake hover text") != NULL);

    CHECK(ase_lsp_client_is_alive(client));
    ase_lsp_client_stop(client);
#endif
}

int main(void) {
    test_missing_server_dies_on_poll();
    test_full_lifecycle();

    printf("all lsp client tests passed\n");
    return 0;
}
