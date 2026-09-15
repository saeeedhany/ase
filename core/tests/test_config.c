#include "test_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/config.h"

#if defined(_WIN32)
#include <direct.h>
#define ase_test_mkdir(p) _mkdir(p)
#define ase_test_rmdir(p) _rmdir(p)
#define ase_test_getcwd(b, n) _getcwd(b, n)
#else
#include <sys/stat.h>
#include <unistd.h>
#define ase_test_mkdir(p) mkdir(p, 0755)
#define ase_test_rmdir(p) rmdir(p)
#define ase_test_getcwd(b, n) getcwd(b, n)
#endif


static void test_defaults(void) {
    AseConfig *config = ase_config_create_default();
    CHECK(config != NULL);

    uint8_t r, g, b, a;
    CHECK(ase_config_get_color(config, "background", &r, &g, &b, &a));
    CHECK(r == 0x28 && g == 0x28 && b == 0x28 && a == 0xFF);

    CHECK(ase_config_get_color(config, "text", &r, &g, &b, &a));
    CHECK(r == 0xF5 && g == 0xE6 && b == 0xC8 && a == 0xFF);

    CHECK(strcmp(ase_config_get_string(config, "font_family"), "monospace") == 0);
    CHECK(ase_config_get_int(config, "font_size", -1) == 11);

    CHECK(ase_config_get_string(config, "no_such_key") == NULL);

    ase_config_destroy(config);
}

static void test_load_missing_file_keeps_defaults(void) {
    AseConfig *config = ase_config_load("this_config_does_not_exist.ase");
    CHECK(config != NULL);
    CHECK(ase_config_get_int(config, "font_size", -1) == 11);
    ase_config_destroy(config);
}

static void test_load_overlays_defaults(void) {
    const char *path = "test_config_overlay.tmp";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs(
        "# a comment, and a blank line above/below\n"
        "\n"
        "background = #101010\n"
        "font_size = 16\n"
        "custom_key = hello world\n"
        "malformed line with no equals\n"
        "  spaced_key   =   spaced value  \n",
        f);
    fclose(f);

    AseConfig *config = ase_config_load(path);
    CHECK(config != NULL);

    uint8_t r, g, b, a;
    CHECK(ase_config_get_color(config, "background", &r, &g, &b, &a));
    CHECK(r == 0x10 && g == 0x10 && b == 0x10);

    /* untouched key keeps its default */
    CHECK(ase_config_get_color(config, "text", &r, &g, &b, &a));
    CHECK(r == 0xF5 && g == 0xE6 && b == 0xC8);

    CHECK(ase_config_get_int(config, "font_size", -1) == 16);
    CHECK(strcmp(ase_config_get_string(config, "custom_key"), "hello world") == 0);
    CHECK(strcmp(ase_config_get_string(config, "spaced_key"), "spaced value") == 0);

    remove(path);
    ase_config_destroy(config);
}

static void test_color_parsing_edge_cases(void) {
    const char *path = "test_config_colors.tmp";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs(
        "no_hash = 282828\n"
        "too_short = #282\n"
        "not_hex = #GGGGGG\n"
        "with_alpha = #28282880\n",
        f);
    fclose(f);

    AseConfig *config = ase_config_load(path);
    uint8_t r, g, b, a;

    CHECK(!ase_config_get_color(config, "no_hash", &r, &g, &b, &a));
    CHECK(!ase_config_get_color(config, "too_short", &r, &g, &b, &a));
    CHECK(!ase_config_get_color(config, "not_hex", &r, &g, &b, &a));

    CHECK(ase_config_get_color(config, "with_alpha", &r, &g, &b, &a));
    CHECK(r == 0x28 && g == 0x28 && b == 0x28 && a == 0x80);

    remove(path);
    ase_config_destroy(config);
}

static void test_write_default_if_missing(void) {
    const char *path = "test_config_write_default.tmp";
    remove(path);

    CHECK(ase_config_write_default_if_missing(path));

    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    char buf[64];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    CHECK(n > 0);
    CHECK(memcmp(buf, "# Absolute Simple Editor", strlen("# Absolute Simple Editor")) == 0);

    /* second call must not overwrite existing content */
    f = fopen(path, "w");
    fputs("sentinel content\n", f);
    fclose(f);

    CHECK(ase_config_write_default_if_missing(path));

    f = fopen(path, "r");
    CHECK(f != NULL);
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    CHECK(memcmp(buf, "sentinel content", 16) == 0);

    remove(path);
}

static void test_default_path_resolves(void) {
    char *path = ase_config_default_path();
    /* HOME/APPDATA should be set in any real environment this runs in;
     * if genuinely absent, NULL is the documented, acceptable result. */
    if (path != NULL) {
        size_t len = strlen(path);
        CHECK(len > strlen("config.ase"));
        CHECK(strcmp(path + len - strlen("config.ase"), "config.ase") == 0);
        free(path);
    }
}

static void test_language_for_path(void) {
    AseConfig *config = ase_config_create_default();
    CHECK(config != NULL);

    CHECK(strcmp(ase_config_language_for_path(config, "a.c"), "c") == 0);
    CHECK(strcmp(ase_config_language_for_path(config, "/tmp/a.h"), "c") == 0);
    CHECK(strcmp(ase_config_language_for_path(config, "a.cpp"), "cpp") == 0);
    CHECK(strcmp(ase_config_language_for_path(config, "a.HXX"), "cpp") == 0);

    CHECK(ase_config_language_for_path(config, "notes.txt") == NULL);
    CHECK(ase_config_language_for_path(config, "Makefile") == NULL);
    CHECK(ase_config_language_for_path(config, NULL) == NULL);
    /* A dot in a parent directory is not a suffix. */
    CHECK(ase_config_language_for_path(config, "/a.c/Makefile") == NULL);
    CHECK(ase_config_language_for_path(config, ".bashrc") == NULL);
    CHECK(ase_config_language_for_path(config, "trailing.") == NULL);

    ase_config_destroy(config);
}

static void test_filetype_override(void) {
    const char *path = "test_config_filetype.tmp";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("filetype.h = cpp\n"
          "filetype.rs = rust\n",
          f);
    fclose(f);

    AseConfig *config = ase_config_load(path);
    CHECK(config != NULL);

    CHECK(strcmp(ase_config_language_for_path(config, "a.h"), "cpp") == 0);
    /* A language with no grammar still resolves — it is the LSP id too. */
    CHECK(strcmp(ase_config_language_for_path(config, "a.rs"), "rust") == 0);
    /* Untouched suffixes keep the built-in answer. */
    CHECK(strcmp(ase_config_language_for_path(config, "a.c"), "c") == 0);

    ase_config_destroy(config);
    remove(path);
}

static void test_lang_string(void) {
    const char *path = "test_config_lang.tmp";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("lang.cpp.lsp = clangd\n"
          "lang.rust.lsp = rust-analyzer\n",
          f);
    fclose(f);

    AseConfig *config = ase_config_load(path);
    CHECK(config != NULL);

    CHECK(strcmp(ase_config_get_lang_string(config, "cpp", "lsp"), "clangd") == 0);
    CHECK(strcmp(ase_config_get_lang_string(config, "rust", "lsp"), "rust-analyzer") == 0);
    /* Unset: the caller falls back to a global key itself. */
    CHECK(ase_config_get_lang_string(config, "c", "lsp") == NULL);
    CHECK(ase_config_get_lang_string(config, "cpp", "no_such") == NULL);
    CHECK(ase_config_get_lang_string(config, NULL, "lsp") == NULL);

    ase_config_destroy(config);
    remove(path);
}

static void test_project_key_allowlist(void) {
    CHECK(ase_config_key_allowed_in_project("filetype.h"));
    CHECK(ase_config_key_allowed_in_project("filetype.rs"));

    /* The rule: what files mean, never what commands to run. */
    CHECK(!ase_config_key_allowed_in_project("lsp_command"));
    CHECK(!ase_config_key_allowed_in_project("build_command"));
    CHECK(!ase_config_key_allowed_in_project("lang.cpp.lsp"));

    /* Default-deny, so anything added later is refused until someone
     * decides otherwise. */
    CHECK(!ase_config_key_allowed_in_project("font_family"));
    CHECK(!ase_config_key_allowed_in_project("background"));
    CHECK(!ase_config_key_allowed_in_project("vim_mode"));
    CHECK(!ase_config_key_allowed_in_project("filetype"));
    CHECK(!ase_config_key_allowed_in_project(""));
    CHECK(!ase_config_key_allowed_in_project(NULL));
}

static void test_project_overlay_refuses_commands(void) {
    const char *path = "test_project_overlay.tmp";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("filetype.h = cpp\n"
          "lsp_command = /bin/evil\n"
          "build_command = rm -rf /\n"
          "lang.cpp.lsp = /bin/evil\n"
          "font_family = Comic Sans\n",
          f);
    fclose(f);

    AseConfig *config = ase_config_create_default();
    CHECK(config != NULL);

    size_t refused = 0;
    CHECK(ase_config_overlay_project(config, path, &refused));
    CHECK(refused == 4);

    CHECK(strcmp(ase_config_language_for_path(config, "a.h"), "cpp") == 0);
    CHECK(ase_config_get_string(config, "lsp_command") == NULL);
    CHECK(ase_config_get_string(config, "build_command") == NULL);
    CHECK(ase_config_get_lang_string(config, "cpp", "lsp") == NULL);
    CHECK(strcmp(ase_config_get_string(config, "font_family"), "monospace") == 0);

    ase_config_destroy(config);
    remove(path);
}

static void test_project_overlay_missing_file(void) {
    AseConfig *config = ase_config_create_default();
    size_t refused = 123;
    CHECK(!ase_config_overlay_project(config, "no_such_project_file.tmp", &refused));
    CHECK(refused == 0);
    CHECK(!ase_config_overlay_project(config, NULL, NULL));
    ase_config_destroy(config);
}

static void test_find_project_file_walks_up(void) {
    /* A file two directories below the one holding .ase.conf. */
    ase_test_mkdir("test_proj");
    ase_test_mkdir("test_proj/sub");
    ase_test_mkdir("test_proj/sub/deeper");

    FILE *f = fopen("test_proj/.ase.conf", "w");
    CHECK(f != NULL);
    fputs("filetype.h = cpp\n", f);
    fclose(f);

    char cwd[1024];
    CHECK(ase_test_getcwd(cwd, sizeof(cwd)) != NULL);

    char start[2048];
    snprintf(start, sizeof(start), "%s/test_proj/sub/deeper/a.h", cwd);
    char *found = ase_config_find_project_file(start);
    CHECK(found != NULL);
    CHECK(strstr(found, "test_proj/.ase.conf") != NULL);
    /* The nearest one wins, not the first from the top. */
    CHECK(strstr(found, "sub") == NULL);
    free(found);

    /* A nearer file takes over. */
    f = fopen("test_proj/sub/.ase.conf", "w");
    CHECK(f != NULL);
    fputs("filetype.h = c\n", f);
    fclose(f);
    found = ase_config_find_project_file(start);
    CHECK(found != NULL);
    CHECK(strstr(found, "sub/.ase.conf") != NULL);
    free(found);

    CHECK(ase_config_find_project_file(NULL) == NULL);

    remove("test_proj/sub/.ase.conf");
    remove("test_proj/.ase.conf");
    ase_test_rmdir("test_proj/sub/deeper");
    ase_test_rmdir("test_proj/sub");
    ase_test_rmdir("test_proj");
}

int main(void) {
    test_defaults();
    test_load_missing_file_keeps_defaults();
    test_load_overlays_defaults();
    test_color_parsing_edge_cases();
    test_write_default_if_missing();
    test_default_path_resolves();
    test_language_for_path();
    test_filetype_override();
    test_lang_string();
    test_project_key_allowlist();
    test_project_overlay_refuses_commands();
    test_project_overlay_missing_file();
    test_find_project_file_walks_up();

    printf("all config tests passed\n");
    return 0;
}
