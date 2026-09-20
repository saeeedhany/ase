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
#include "ase/plugin_host.h"

static void noop_command(AseBuffer *buffer, void *user_data) {
    (void)buffer;
    (void)user_data;
}

static void test_register_and_run(void) {
    TRACE("before host_create");
    AsePluginHost *host = ase_plugin_host_create();
    TRACE("host_create returned %p", (void *)host);
    CHECK(host != NULL);
    CHECK(ase_plugin_host_command_count(host) == 0);
    TRACE("before register_command");
    CHECK(ase_plugin_host_register_command(host, "noop", noop_command, NULL));
    TRACE("register_command ok");
    CHECK(ase_plugin_host_command_count(host) == 1);

    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_plugin_host_run_command(host, "noop", buf));
    CHECK(!ase_plugin_host_run_command(host, "does_not_exist", buf));

    /* re-registering the same name replaces, doesn't grow the count */
    CHECK(ase_plugin_host_register_command(host, "noop", noop_command, NULL));
    CHECK(ase_plugin_host_command_count(host) == 1);

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

    int loaded = ase_plugin_host_load_directory(host, ASE_TEST_FIXTURES_DIR);
    CHECK(loaded == 2); /* uppercase.lua + the compiled test_native_plugin */
    CHECK(ase_plugin_host_command_count(host) == 2);

    AseBuffer *buf = ase_buffer_create();
    CHECK(ase_buffer_insert(buf, 0, "hello", 5));

    CHECK(ase_plugin_host_run_command(host, "lua_uppercase", buf));
    char out[16];
    size_t n = ase_buffer_get_text(buf, 0, ase_buffer_length(buf), out);
    CHECK(n == 5 && memcmp(out, "HELLO", 5) == 0);

    CHECK(ase_plugin_host_run_command(host, "native_reverse", buf));
    n = ase_buffer_get_text(buf, 0, ase_buffer_length(buf), out);
    CHECK(n == 5 && memcmp(out, "OLLEH", 5) == 0);

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

int main(void) {
    RUN(test_register_and_run);
    RUN(test_load_directory_missing_is_not_an_error);
    RUN(test_lua_and_native_plugins_from_directory);
    RUN(test_broken_lua_script_is_skipped_not_fatal);

    printf("all plugin host tests passed\n");
    return 0;
}
