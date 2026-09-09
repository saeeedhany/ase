#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/config.h"

static void test_defaults(void) {
    AseConfig *config = ase_config_create_default();
    assert(config != NULL);

    uint8_t r, g, b, a;
    assert(ase_config_get_color(config, "background", &r, &g, &b, &a));
    assert(r == 0x28 && g == 0x28 && b == 0x28 && a == 0xFF);

    assert(ase_config_get_color(config, "text", &r, &g, &b, &a));
    assert(r == 0xF5 && g == 0xE6 && b == 0xC8 && a == 0xFF);

    assert(strcmp(ase_config_get_string(config, "font_family"), "monospace") == 0);
    assert(ase_config_get_int(config, "font_size", -1) == 12);

    assert(ase_config_get_string(config, "no_such_key") == NULL);

    ase_config_destroy(config);
}

static void test_load_missing_file_keeps_defaults(void) {
    AseConfig *config = ase_config_load("this_config_does_not_exist.ase");
    assert(config != NULL);
    assert(ase_config_get_int(config, "font_size", -1) == 12);
    ase_config_destroy(config);
}

static void test_load_overlays_defaults(void) {
    const char *path = "test_config_overlay.tmp";
    FILE *f = fopen(path, "w");
    assert(f != NULL);
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
    assert(config != NULL);

    uint8_t r, g, b, a;
    assert(ase_config_get_color(config, "background", &r, &g, &b, &a));
    assert(r == 0x10 && g == 0x10 && b == 0x10);

    /* untouched key keeps its default */
    assert(ase_config_get_color(config, "text", &r, &g, &b, &a));
    assert(r == 0xF5 && g == 0xE6 && b == 0xC8);

    assert(ase_config_get_int(config, "font_size", -1) == 16);
    assert(strcmp(ase_config_get_string(config, "custom_key"), "hello world") == 0);
    assert(strcmp(ase_config_get_string(config, "spaced_key"), "spaced value") == 0);

    remove(path);
    ase_config_destroy(config);
}

static void test_color_parsing_edge_cases(void) {
    const char *path = "test_config_colors.tmp";
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs(
        "no_hash = 282828\n"
        "too_short = #282\n"
        "not_hex = #GGGGGG\n"
        "with_alpha = #28282880\n",
        f);
    fclose(f);

    AseConfig *config = ase_config_load(path);
    uint8_t r, g, b, a;

    assert(!ase_config_get_color(config, "no_hash", &r, &g, &b, &a));
    assert(!ase_config_get_color(config, "too_short", &r, &g, &b, &a));
    assert(!ase_config_get_color(config, "not_hex", &r, &g, &b, &a));

    assert(ase_config_get_color(config, "with_alpha", &r, &g, &b, &a));
    assert(r == 0x28 && g == 0x28 && b == 0x28 && a == 0x80);

    remove(path);
    ase_config_destroy(config);
}

static void test_write_default_if_missing(void) {
    const char *path = "test_config_write_default.tmp";
    remove(path);

    assert(ase_config_write_default_if_missing(path));

    FILE *f = fopen(path, "r");
    assert(f != NULL);
    char buf[64];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    assert(n > 0);
    assert(memcmp(buf, "# Absolute Simple Editor", strlen("# Absolute Simple Editor")) == 0);

    /* second call must not overwrite existing content */
    f = fopen(path, "w");
    fputs("sentinel content\n", f);
    fclose(f);

    assert(ase_config_write_default_if_missing(path));

    f = fopen(path, "r");
    assert(f != NULL);
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    assert(memcmp(buf, "sentinel content", 16) == 0);

    remove(path);
}

static void test_default_path_resolves(void) {
    char *path = ase_config_default_path();
    /* HOME/APPDATA should be set in any real environment this runs in;
     * if genuinely absent, NULL is the documented, acceptable result. */
    if (path != NULL) {
        size_t len = strlen(path);
        assert(len > strlen("config.ase"));
        assert(strcmp(path + len - strlen("config.ase"), "config.ase") == 0);
        free(path);
    }
}

int main(void) {
    test_defaults();
    test_load_missing_file_keeps_defaults();
    test_load_overlays_defaults();
    test_color_parsing_edge_cases();
    test_write_default_if_missing();
    test_default_path_resolves();

    printf("all config tests passed\n");
    return 0;
}
