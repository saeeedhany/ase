#include "test_assert.h"
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define ASE_MKDIR(path) _mkdir(path)
#define ASE_RMDIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define ASE_MKDIR(path) mkdir(path, 0755)
#define ASE_RMDIR(path) rmdir(path)
#endif

#include "ase/buffer.h"
#include "ase/editor_context.h"
#include "ase/plugin_host.h"

/*
 * A stand-in editor: what the GUI provides in the real app, reduced to
 * plain fields a test can read back. See docs/adr/0141.
 */
typedef struct {
    AseBuffer *buffer;
    size_t cursor;
    size_t sel_start;
    size_t sel_end;
    bool has_selection;
    char status[64];
} FakeEditor;

static AseBuffer *fake_buffer(void *host) { return ((FakeEditor *)host)->buffer; }
static size_t fake_cursor(void *host) { return ((FakeEditor *)host)->cursor; }
static void fake_set_cursor(void *host, size_t at) { ((FakeEditor *)host)->cursor = at; }

static bool fake_selection(void *host, size_t *start, size_t *end) {
    FakeEditor *editor = (FakeEditor *)host;
    if (!editor->has_selection) {
        return false;
    }
    *start = editor->sel_start;
    *end = editor->sel_end;
    return true;
}

static void fake_set_selection(void *host, size_t start, size_t end) {
    FakeEditor *editor = (FakeEditor *)host;
    editor->sel_start = start;
    editor->sel_end = end;
    editor->has_selection = true;
}

static const char *fake_config(void *host, const char *key) {
    (void)host;
    return strcmp(key, "tab_width") == 0 ? "4" : NULL;
}

static void fake_status(void *host, const char *message) {
    snprintf(((FakeEditor *)host)->status, sizeof(((FakeEditor *)host)->status), "%s", message);
}

static const AseEditorContextOps kFakeOps = {fake_buffer,        fake_cursor, fake_set_cursor,
                                             fake_selection,     fake_set_selection,
                                             fake_config,        fake_status};

/* Hooks that record what happened, so order and count are assertable. */
static char g_trace[128];

static void trace(const char *what) {
    size_t len = strlen(g_trace);
    snprintf(g_trace + len, sizeof(g_trace) - len, "%s%s", len > 0 ? "," : "", what);
}

static void hook_a(AseEditorContext *ctx, void *user_data) {
    (void)ctx;
    (void)user_data;
    trace("a");
}

static void hook_b(AseEditorContext *ctx, void *user_data) {
    (void)ctx;
    (void)user_data;
    trace("b");
}

static void hook_carrying_data(AseEditorContext *ctx, void *user_data) {
    (void)ctx;
    trace((const char *)user_data);
}

static AsePluginHost *g_reentrant_host = NULL;

/* The hook every guard exists for: it causes the event it is handling. */
static void hook_that_emits_again(AseEditorContext *ctx, void *user_data) {
    (void)user_data;
    trace("outer");
    ase_plugin_host_emit(g_reentrant_host, ASE_EVENT_BUFFER_CHANGED, ctx);
}

/* And the two-event version: buffer_changed moves the caret, whose hook
 * edits the buffer. One flag for all events is what stops this. */
static void hook_emits_other_event(AseEditorContext *ctx, void *user_data) {
    (void)user_data;
    trace("changed");
    ase_plugin_host_emit(g_reentrant_host, ASE_EVENT_CURSOR_MOVED, ctx);
}

static void hook_emits_back(AseEditorContext *ctx, void *user_data) {
    (void)user_data;
    trace("moved");
    ase_plugin_host_emit(g_reentrant_host, ASE_EVENT_BUFFER_CHANGED, ctx);
}

static void noop_command(AseEditorContext *ctx, void *user_data) {
    (void)ctx;
    (void)user_data;
}

static void test_register_and_run(void) {
    AsePluginHost *host = ase_plugin_host_create();
    CHECK(host != NULL);
    CHECK(ase_plugin_host_command_count(host) == 0);
    CHECK(ase_plugin_host_register_command(host, "noop", noop_command, NULL));
    CHECK(ase_plugin_host_command_count(host) == 1);

    AseBuffer *buf = ase_buffer_create();
    FakeEditor editor = {buf, 0, 0, 0, false, {0}};
    AseEditorContext *ctx = ase_editor_context_create(&kFakeOps, &editor);
    CHECK(ctx != NULL);
    CHECK(ase_plugin_host_run_command(host, "noop", ctx));
    CHECK(!ase_plugin_host_run_command(host, "does_not_exist", ctx));
    /* A command must survive being handed no editor at all. */
    CHECK(ase_plugin_host_run_command(host, "noop", NULL));

    /* re-registering the same name replaces, doesn't grow the count */
    CHECK(ase_plugin_host_register_command(host, "noop", noop_command, NULL));
    CHECK(ase_plugin_host_command_count(host) == 1);

    ase_editor_context_destroy(ctx);
    ase_buffer_destroy(buf);
    ase_plugin_host_destroy(host);
}

static void test_load_directory_missing_is_not_an_error(void) {
    AsePluginHost *host = ase_plugin_host_create();
    CHECK(host != NULL);
    CHECK(ase_plugin_host_load_directory(host, "this_directory_does_not_exist") == 0);
    CHECK(ase_plugin_host_command_count(host) == 0);
    ase_plugin_host_destroy(host);
}

static void test_lua_and_native_plugins_from_directory(void) {
    AsePluginHost *host = ase_plugin_host_create();
    CHECK(host != NULL);

    /* Three modules are in the directory; the third is built without
     * the ABI 2 version symbol and must be refused. */
    int loaded = ase_plugin_host_load_directory(host, ASE_TEST_FIXTURES_DIR);
    CHECK(loaded == 2); /* uppercase.lua + the compiled test_native_plugin */
    CHECK(ase_plugin_host_command_count(host) == 2);
    /* Refused before its register function ran, not after: the command
     * it would have registered is not there. */
    CHECK(!ase_plugin_host_run_command(host, "stale_command", NULL));

    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "hello", 5));
    FakeEditor editor = {buf, 3, 0, 0, false, {0}};
    AseEditorContext *ctx = ase_editor_context_create(&kFakeOps, &editor);

    /* uppercase.lua was written against ABI 1 and has not been touched:
     * it passes the value it is handed to ase.buffer_*, which is the
     * context now. Proving that still works is the point of this line. */
    CHECK(ase_plugin_host_run_command(host, "lua_uppercase", ctx));
    char out[16];
    size_t n = ase_buffer_get_text(buf, 0, ase_buffer_length(buf), out);
    CHECK(n == 5 && memcmp(out, "HELLO", 5) == 0);

    CHECK(ase_plugin_host_run_command(host, "native_reverse", ctx));
    n = ase_buffer_get_text(buf, 0, ase_buffer_length(buf), out);
    CHECK(n == 5 && memcmp(out, "OLLEH", 5) == 0);
    /* The native plugin reached past the text and moved the caret and
     * the status line, which ABI 1 gave it no way to do. */
    CHECK(editor.cursor == 0);
    CHECK(strcmp(editor.status, "reversed") == 0);

    ase_editor_context_destroy(ctx);
    ase_buffer_destroy(buf);
    ase_plugin_host_destroy(host);
}

static void test_broken_lua_script_is_skipped_not_fatal(void) {
    const char *dir = "test_broken_plugin_dir.tmp";
    ASE_MKDIR(dir);

    char path[256];
    snprintf(path, sizeof(path), "%s/broken.lua", dir);
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("this is not valid lua syntax )))\n", f);
    fclose(f);

    AsePluginHost *host = ase_plugin_host_create();
    CHECK(host != NULL);

    /* Must not crash, and the broken script must not count as loaded. */
    int loaded = ase_plugin_host_load_directory(host, dir);
    CHECK(loaded == 0);
    CHECK(ase_plugin_host_command_count(host) == 0);

    ase_plugin_host_destroy(host);
    remove(path);
    ASE_RMDIR(dir);
}

/* Every accessor answers, and answers nothing rather than crashing when
 * the host does not implement it. */
static void test_context_accessors(void) {
    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "hello", 5));
    FakeEditor editor = {buf, 2, 0, 0, false, {0}};
    AseEditorContext *ctx = ase_editor_context_create(&kFakeOps, &editor);
    CHECK(ctx != NULL);

    CHECK(ase_ctx_buffer(ctx) == buf);
    CHECK(ase_ctx_cursor(ctx) == 2);
    ase_ctx_set_cursor(ctx, 4);
    CHECK(ase_ctx_cursor(ctx) == 4);

    size_t start = 99;
    size_t end = 99;
    CHECK(!ase_ctx_selection(ctx, &start, &end));
    CHECK(start == 99 && end == 99); /* untouched when nothing is selected */

    /* Backwards in, ordered out: a plugin should not have to sort. */
    ase_ctx_set_selection(ctx, 4, 1);
    CHECK(ase_ctx_selection(ctx, &start, &end));
    CHECK(start == 1 && end == 4);

    CHECK(strcmp(ase_ctx_config(ctx, "tab_width"), "4") == 0);
    CHECK(ase_ctx_config(ctx, "no_such_key") == NULL);

    ase_ctx_status(ctx, "hi");
    CHECK(strcmp(editor.status, "hi") == 0);

    ase_editor_context_destroy(ctx);
    ase_buffer_destroy(buf);
}

/* A host that implements nothing is a valid host. Without this every
 * accessor would need its own NULL check at every call site. */
static void test_context_tolerates_an_empty_host(void) {
    static const AseEditorContextOps kNothing = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    AseEditorContext *ctx = ase_editor_context_create(&kNothing, NULL);
    CHECK(ctx != NULL);

    CHECK(ase_ctx_buffer(ctx) == NULL);
    CHECK(ase_ctx_cursor(ctx) == 0);
    size_t start = 0;
    size_t end = 0;
    CHECK(!ase_ctx_selection(ctx, &start, &end));
    CHECK(ase_ctx_config(ctx, "anything") == NULL);
    ase_ctx_set_cursor(ctx, 5);
    ase_ctx_set_selection(ctx, 1, 2);
    ase_ctx_status(ctx, "ignored");

    ase_editor_context_destroy(ctx);

    /* And a NULL context is the same again, one level up. */
    CHECK(ase_ctx_buffer(NULL) == NULL);
    CHECK(ase_ctx_cursor(NULL) == 0);
    CHECK(!ase_ctx_selection(NULL, &start, &end));
    ase_ctx_status(NULL, "ignored");
    ase_editor_context_destroy(NULL);
}

static void test_event_names_round_trip(void) {
    AseEventKind event;
    CHECK(ase_event_from_name("buffer_changed", &event) && event == ASE_EVENT_BUFFER_CHANGED);
    CHECK(ase_event_from_name("cursor_moved", &event) && event == ASE_EVENT_CURSOR_MOVED);
    CHECK(ase_event_from_name("file_saved", &event) && event == ASE_EVENT_FILE_SAVED);
    CHECK(ase_event_from_name("file_opened", &event) && event == ASE_EVENT_FILE_OPENED);
    CHECK(!ase_event_from_name("on_the_third_tuesday", &event));
    CHECK(!ase_event_from_name(NULL, &event));

    /* Every kind has a name, and it is the one that parses back. */
    for (int i = 0; i < ASE_EVENT_COUNT; i++) {
        const char *name = ase_event_name((AseEventKind)i);
        CHECK(name != NULL && name[0] != '\0');
        CHECK(ase_event_from_name(name, &event) && event == (AseEventKind)i);
    }
    CHECK(ase_event_name(ASE_EVENT_COUNT) == NULL);
}

static void test_hooks_run_in_order(void) {
    AsePluginHost *host = ase_plugin_host_create();
    CHECK(host != NULL);
    CHECK(ase_plugin_host_hook_count(host, ASE_EVENT_FILE_SAVED) == 0);

    CHECK(ase_plugin_host_on(host, ASE_EVENT_FILE_SAVED, hook_a, NULL));
    CHECK(ase_plugin_host_on(host, ASE_EVENT_FILE_SAVED, hook_b, NULL));
    CHECK(ase_plugin_host_on(host, ASE_EVENT_FILE_OPENED, hook_a, NULL));
    CHECK(ase_plugin_host_hook_count(host, ASE_EVENT_FILE_SAVED) == 2);

    g_trace[0] = '\0';
    ase_plugin_host_emit(host, ASE_EVENT_FILE_SAVED, NULL);
    CHECK(strcmp(g_trace, "a,b") == 0);

    /* An event nobody hooked is silence, not the other event's hooks. */
    g_trace[0] = '\0';
    ase_plugin_host_emit(host, ASE_EVENT_CURSOR_MOVED, NULL);
    CHECK(g_trace[0] == '\0');

    /* Each hook gets its own user_data back. */
    CHECK(ase_plugin_host_on(host, ASE_EVENT_CURSOR_MOVED, hook_carrying_data, (void *)"mine"));
    g_trace[0] = '\0';
    ase_plugin_host_emit(host, ASE_EVENT_CURSOR_MOVED, NULL);
    CHECK(strcmp(g_trace, "mine") == 0);

    ase_plugin_host_destroy(host);
}

static void test_a_hook_cannot_re_enter(void) {
    AsePluginHost *host = ase_plugin_host_create();
    CHECK(host != NULL);
    g_reentrant_host = host;

    CHECK(ase_plugin_host_on(host, ASE_EVENT_BUFFER_CHANGED, hook_that_emits_again, NULL));
    g_trace[0] = '\0';
    ase_plugin_host_emit(host, ASE_EVENT_BUFFER_CHANGED, NULL);
    /* Once. Without the guard this recurses until the stack runs out. */
    CHECK(strcmp(g_trace, "outer") == 0);

    ase_plugin_host_destroy(host);
    g_reentrant_host = NULL;
}

static void test_two_hooks_cannot_bounce(void) {
    AsePluginHost *host = ase_plugin_host_create();
    CHECK(host != NULL);
    g_reentrant_host = host;

    CHECK(ase_plugin_host_on(host, ASE_EVENT_BUFFER_CHANGED, hook_emits_other_event, NULL));
    CHECK(ase_plugin_host_on(host, ASE_EVENT_CURSOR_MOVED, hook_emits_back, NULL));

    g_trace[0] = '\0';
    ase_plugin_host_emit(host, ASE_EVENT_BUFFER_CHANGED, NULL);
    /* The second event is dropped too: the guard is one flag for all of
     * them, or this pair loops forever. */
    CHECK(strcmp(g_trace, "changed") == 0);

    /* And the host still works afterwards — the flag was cleared. */
    g_trace[0] = '\0';
    ase_plugin_host_emit(host, ASE_EVENT_CURSOR_MOVED, NULL);
    CHECK(strcmp(g_trace, "moved") == 0);

    ase_plugin_host_destroy(host);
    g_reentrant_host = NULL;
}

static void test_emit_tolerates_nonsense(void) {
    AsePluginHost *host = ase_plugin_host_create();
    CHECK(host != NULL);

    CHECK(!ase_plugin_host_on(NULL, ASE_EVENT_FILE_SAVED, hook_a, NULL));
    CHECK(!ase_plugin_host_on(host, ASE_EVENT_FILE_SAVED, NULL, NULL));
    CHECK(!ase_plugin_host_on(host, ASE_EVENT_COUNT, hook_a, NULL));
    CHECK(ase_plugin_host_hook_count(NULL, ASE_EVENT_FILE_SAVED) == 0);
    CHECK(ase_plugin_host_hook_count(host, ASE_EVENT_COUNT) == 0);

    ase_plugin_host_emit(NULL, ASE_EVENT_FILE_SAVED, NULL);
    ase_plugin_host_emit(host, ASE_EVENT_COUNT, NULL);

    ase_plugin_host_destroy(host);
}

int main(void) {
    RUN(test_event_names_round_trip);
    RUN(test_hooks_run_in_order);
    RUN(test_a_hook_cannot_re_enter);
    RUN(test_two_hooks_cannot_bounce);
    RUN(test_emit_tolerates_nonsense);
    RUN(test_context_accessors);
    RUN(test_context_tolerates_an_empty_host);
    RUN(test_register_and_run);
    RUN(test_load_directory_missing_is_not_an_error);
    RUN(test_lua_and_native_plugins_from_directory);
    RUN(test_broken_lua_script_is_skipped_not_fatal);

    printf("all plugin host tests passed\n");
    return 0;
}
