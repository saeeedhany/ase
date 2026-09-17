/*
 * Fake LSP server for core/tests/test_lsp_client.c. Deliberately does
 * NOT link against ase_core / ase/json.h — it's a standalone black-box
 * stand-in with its own naive substring-based message handling, so a
 * bug in the real JSON module can't mask itself symmetrically on both
 * ends of the test (see docs/adr/0011, decision 7).
 *
 * Handles exactly the methods the real client sends: initialize,
 * initialized, textDocument/didOpen (replies with a canned
 * publishDiagnostics notification), textDocument/didChange (replies
 * with a *different*, empty publishDiagnostics — simulating "the
 * error was just fixed," so a test can prove didChange actually
 * refreshes diagnostics rather than leaving the didOpen-time snapshot
 * stale — see docs/adr/0029), textDocument/completion,
 * textDocument/definition, textDocument/references,
 * textDocument/documentSymbol, textDocument/hover, shutdown, exit.
 * Anything else is silently ignored, matching how a real server
 * tolerates unknown methods.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_message(void) {
    char header_line[256];
    long content_length = -1;

    for (;;) {
        if (fgets(header_line, sizeof(header_line), stdin) == NULL) {
            return NULL; /* EOF */
        }
        size_t len = strlen(header_line);
        while (len > 0 && (header_line[len - 1] == '\n' || header_line[len - 1] == '\r')) {
            header_line[--len] = '\0';
        }
        if (len == 0) {
            break; /* blank line: end of headers */
        }
        long value;
        if (sscanf(header_line, "Content-Length: %ld", &value) == 1) {
            content_length = value;
        }
    }

    if (content_length < 0) {
        return NULL;
    }

    char *body = (char *)malloc((size_t)content_length + 1);
    if (body == NULL) {
        return NULL;
    }
    size_t total_read = fread(body, 1, (size_t)content_length, stdin);
    body[total_read] = '\0';
    if (total_read != (size_t)content_length) {
        free(body);
        return NULL;
    }
    return body;
}

static int has_method(const char *body, const char *method) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"method\":\"%s\"", method);
    return strstr(body, needle) != NULL;
}

static long extract_id(const char *body) {
    const char *p = strstr(body, "\"id\":");
    if (p == NULL) {
        return -1;
    }
    return strtol(p + strlen("\"id\":"), NULL, 10);
}

static void send_message(const char *body) {
    printf("Content-Length: %zu\r\n\r\n%s", strlen(body), body);
    fflush(stdout);
}

int main(void) {
    for (;;) {
        char *body = read_message();
        if (body == NULL) {
            break;
        }

        if (has_method(body, "initialize")) {
            long id = extract_id(body);
            char response[256];
            snprintf(response, sizeof(response),
                     "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{\"capabilities\":{}}}", id);
            send_message(response);
        } else if (has_method(body, "textDocument/didOpen")) {
            send_message(
                "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
                "\"params\":{\"uri\":\"file:///fake.txt\",\"diagnostics\":["
                "{\"range\":{\"start\":{\"line\":0,\"character\":0},"
                "\"end\":{\"line\":0,\"character\":5}},"
                "\"severity\":1,\"message\":\"fake diagnostic\"}]}}");
        } else if (has_method(body, "textDocument/didChange")) {
            send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
                         "\"params\":{\"uri\":\"file:///fake.txt\",\"diagnostics\":[]}}");
        } else if (has_method(body, "textDocument/completion")) {
            long id = extract_id(body);
            char response[512];
            snprintf(response, sizeof(response),
                     "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{\"isIncomplete\":false,"
                     "\"items\":[{\"label\":\"fake_completion_item\"}]}}",
                     id);
            send_message(response);
        } else if (has_method(body, "textDocument/definition")) {
            long id = extract_id(body);
            char response[512];
            snprintf(response, sizeof(response),
                     "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{\"uri\":\"file:///fake.txt\","
                     "\"range\":{\"start\":{\"line\":2,\"character\":4},"
                     "\"end\":{\"line\":2,\"character\":10}}}}",
                     id);
            send_message(response);
        } else if (has_method(body, "textDocument/references")) {
            long id = extract_id(body);
            char response[768];
            /* Two, in different files, so a caller that only reads the
             * first is caught. */
            snprintf(response, sizeof(response),
                     "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":["
                     "{\"uri\":\"file:///fake.txt\","
                     "\"range\":{\"start\":{\"line\":2,\"character\":4},"
                     "\"end\":{\"line\":2,\"character\":10}}},"
                     "{\"uri\":\"file:///other.txt\","
                     "\"range\":{\"start\":{\"line\":9,\"character\":0},"
                     "\"end\":{\"line\":9,\"character\":6}}}]}",
                     id);
            send_message(response);
        } else if (has_method(body, "textDocument/documentSymbol")) {
            long id = extract_id(body);
            char response[768];
            /* The nested DocumentSymbol shape, with a child, since a
             * caller that only walks the top level is the likely bug. */
            snprintf(response, sizeof(response),
                     "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":["
                     "{\"name\":\"outer_symbol\",\"kind\":12,"
                     "\"range\":{\"start\":{\"line\":1,\"character\":0},"
                     "\"end\":{\"line\":5,\"character\":1}},"
                     "\"selectionRange\":{\"start\":{\"line\":1,\"character\":5},"
                     "\"end\":{\"line\":1,\"character\":18}},"
                     "\"children\":[{\"name\":\"inner_symbol\",\"kind\":13,"
                     "\"range\":{\"start\":{\"line\":3,\"character\":2},"
                     "\"end\":{\"line\":3,\"character\":20}},"
                     "\"selectionRange\":{\"start\":{\"line\":3,\"character\":6},"
                     "\"end\":{\"line\":3,\"character\":18}}}]}]}",
                     id);
            send_message(response);
        } else if (has_method(body, "textDocument/hover")) {
            long id = extract_id(body);
            char response[512];
            snprintf(response, sizeof(response),
                     "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{\"contents\":"
                     "{\"kind\":\"plaintext\",\"value\":\"fake hover text\"}}}",
                     id);
            send_message(response);
        } else if (has_method(body, "shutdown")) {
            long id = extract_id(body);
            char response[128];
            snprintf(response, sizeof(response), "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":null}", id);
            send_message(response);
        } else if (has_method(body, "exit")) {
            free(body);
            return 0;
        }
        /* "initialized" and any other unknown method: no response needed */

        free(body);
    }
    return 0;
}
