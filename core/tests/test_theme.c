#include "test_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* <strings.h> does not exist on MSVC, and strcasecmp is spelled
 * _stricmp there. See docs/adr/0130. */
#if defined(_WIN32)
#define ase_test_strcasecmp _stricmp
#else
#include <strings.h>
#define ase_test_strcasecmp strcasecmp
#endif

#include "ase/theme.h"

static const char *kPath = "test_theme.tmp";

static AseConfig *config_from(const char *text) {
    FILE *f = fopen(kPath, "w");
    CHECK(f != NULL);
    fputs(text, f);
    fclose(f);
    AseConfig *config = ase_config_load(kPath);
    remove(kPath);
    return config;
}

static void expect_colour(const AseConfig *config, const char *key, const char *want) {
    const char *got = ase_config_get_string(config, key);
    if (got == NULL || strcmp(got, want) != 0) {
        printf("%s is '%s', expected '%s'\n", key, got ? got : "(unset)", want);
        fflush(stdout);
        CHECK(0);
    }
}

static void test_every_theme_is_complete(void) {
    CHECK(ase_theme_count() >= 3);
    for (size_t i = 0; i < ase_theme_count(); i++) {
        const AseTheme *theme = ase_theme_at(i);
        CHECK(theme != NULL);
        /* A theme missing a colour would leave the previous theme's
         * showing through, which reads as a rendering bug. */
        const char *fields[] = {theme->name,          theme->summary,
                                theme->background,    theme->text,
                                theme->selection,     theme->find_match,
                                theme->panel_background, theme->syntax_type,
                                theme->syntax_string, theme->diagnostic_error,
                                theme->diagnostic_warning};
        for (size_t f = 0; f < sizeof(fields) / sizeof(fields[0]); f++) {
            if (fields[f] == NULL || fields[f][0] == '\0') {
                printf("theme '%s' has an empty field %zu\n", theme->name, f);
                fflush(stdout);
                CHECK(0);
            }
        }
        /* Every colour must actually parse, or the editor falls back to
         * whatever it had and the theme silently half-applies. */
        uint8_t r, g, b, a;
        AseConfig *probe = ase_config_create_default();
        CHECK(ase_config_apply_theme(probe, theme->name));
        CHECK(ase_config_get_color(probe, "background", &r, &g, &b, &a));
        CHECK(ase_config_get_color(probe, "text", &r, &g, &b, &a));
        CHECK(ase_config_get_color(probe, "syntax_type", &r, &g, &b, &a));
        CHECK(ase_config_get_color(probe, "syntax_string", &r, &g, &b, &a));
        CHECK(ase_config_get_color(probe, "diagnostic_error", &r, &g, &b, &a));
        CHECK(ase_config_get_color(probe, "diagnostic_warning", &r, &g, &b, &a));
        CHECK(ase_config_get_color(probe, "selection", &r, &g, &b, &a));
        ase_config_destroy(probe);
    }
}

static void test_names_are_unique(void) {
    for (size_t i = 0; i < ase_theme_count(); i++) {
        for (size_t j = i + 1; j < ase_theme_count(); j++) {
            CHECK(strcmp(ase_theme_at(i)->name, ase_theme_at(j)->name) != 0);
        }
    }
}

static void test_lookup(void) {
    CHECK(ase_theme_find("simple-nord") != NULL);
    CHECK(ase_theme_find("ase-default") != NULL);
    CHECK(ase_theme_find("no-such-theme") == NULL);
    CHECK(ase_theme_find(NULL) == NULL);
    CHECK(ase_theme_at(9999) == NULL);
}

static void test_applying_replaces_the_defaults(void) {
    AseConfig *config = ase_config_create_default();
    CHECK(ase_config_apply_theme(config, "simple-nord"));
    expect_colour(config, "background", "#2E3440");
    expect_colour(config, "text", "#D8DEE9");
    expect_colour(config, "syntax_type", "#8FBCBB");
    expect_colour(config, "diagnostic_error", "#BF616A");
    ase_config_destroy(config);
}

/* The whole point of the layering: picking a theme must not undo a
 * colour someone chose by hand. */
static void test_a_hand_set_colour_survives(void) {
    AseConfig *config = config_from("text = #FFFFFF\n");
    CHECK(ase_config_apply_theme(config, "simple-nord"));
    expect_colour(config, "text", "#FFFFFF");       /* theirs */
    expect_colour(config, "background", "#2E3440"); /* the theme's */
    ase_config_destroy(config);
}

/* Switching between themes must not accumulate: the second theme's
 * colours replace the first's entirely. */
static void test_switching_themes_leaves_nothing_behind(void) {
    AseConfig *config = ase_config_create_default();
    CHECK(ase_config_apply_theme(config, "simple-nord"));
    CHECK(ase_config_apply_theme(config, "simple-sola"));
    expect_colour(config, "background", "#002B36");
    expect_colour(config, "text", "#93A1A1");
    expect_colour(config, "syntax_type", "#2AA198");
    expect_colour(config, "diagnostic_error", "#DC322F");
    expect_colour(config, "selection", "#07364266");
    ase_config_destroy(config);
}

/* ...and a hand-set colour still survives that. */
static void test_hand_set_survives_switching(void) {
    AseConfig *config = config_from("syntax_type = #ABCDEF\n");
    CHECK(ase_config_apply_theme(config, "simple-nord"));
    CHECK(ase_config_apply_theme(config, "simple-sola"));
    expect_colour(config, "syntax_type", "#ABCDEF");
    ase_config_destroy(config);
}

static void test_unknown_theme_changes_nothing(void) {
    AseConfig *config = ase_config_create_default();
    const char *before = ase_config_get_string(config, "background");
    char kept[32];
    snprintf(kept, sizeof(kept), "%s", before ? before : "");
    CHECK(!ase_config_apply_theme(config, "no-such-theme"));
    expect_colour(config, "background", kept);
    CHECK(!ase_config_apply_theme(NULL, "simple-nord"));
    ase_config_destroy(config);
}

/* ase-default has to be the defaults, or launching with it selected
 * would change the editor's look. */
static void test_default_theme_matches_the_defaults(void) {
    AseConfig *plain = ase_config_create_default();
    AseConfig *themed = ase_config_create_default();
    CHECK(ase_config_apply_theme(themed, "ase-default"));

    static const char *kColours[] = {"background",       "text",
                                     "selection",        "find_match",
                                     "panel_background", "syntax_type",
                                     "syntax_string",    "diagnostic_error",
                                     "diagnostic_warning"};
    for (size_t i = 0; i < sizeof(kColours) / sizeof(kColours[0]); i++) {
        const char *a = ase_config_get_string(plain, kColours[i]);
        const char *b = ase_config_get_string(themed, kColours[i]);
        CHECK(a != NULL && b != NULL);
        if (ase_test_strcasecmp(a, b) != 0) {
            printf("ase-default changes %s: %s vs %s\n", kColours[i], a, b);
            fflush(stdout);
            CHECK(0);
        }
    }
    ase_config_destroy(plain);
    ase_config_destroy(themed);
}

/* The reported bug. Every config.ase written before themes existed
 * contains all nine colours, because the starter file wrote them. If
 * being in the file counted as a choice, a theme would change nothing
 * for anyone who had ever run the editor — which is what happened. */
static void test_a_config_full_of_shipped_defaults_still_themes(void) {
    AseConfig *config = config_from(
        "background = #282828\n"
        "text = #F5E6C8\n"
        "selection = #45403866\n"
        "find_match = #45403899\n"
        "panel_background = #282828E6\n"
        "diagnostic_error = #E06C75\n"
        "diagnostic_warning = #E5C07B\n"
        "syntax_type = #689d6a\n"   /* the starter file's own lowercase */
        "syntax_string = #d79921\n");
    CHECK(ase_config_apply_theme(config, "simple-nord"));
    expect_colour(config, "background", "#2E3440");
    expect_colour(config, "text", "#D8DEE9");
    expect_colour(config, "selection", "#434C5E66");
    expect_colour(config, "syntax_type", "#8FBCBB");
    expect_colour(config, "syntax_string", "#EBCB8B");
    expect_colour(config, "diagnostic_error", "#BF616A");

    /* And none of them counts as a hand-made choice, so nothing is
     * reported as shadowing the theme. */
    static const char *kColours[] = {"background", "text", "selection", "find_match",
                                     "panel_background", "syntax_type", "syntax_string",
                                     "diagnostic_error", "diagnostic_warning"};
    for (size_t i = 0; i < sizeof(kColours) / sizeof(kColours[0]); i++) {
        if (ase_config_is_chosen_by_hand(config, kColours[i])) {
            printf("%s counted as hand-chosen when it is the shipped value\n", kColours[i]);
            fflush(stdout);
            CHECK(0);
        }
    }
    ase_config_destroy(config);
}

/* ...and one genuinely changed colour among them is still honoured. */
static void test_one_real_choice_among_defaults_survives(void) {
    AseConfig *config = config_from(
        "background = #282828\n"      /* shipped */
        "text = #FF00FF\n"            /* theirs */
        "syntax_type = #689d6a\n");   /* shipped */
    CHECK(ase_config_apply_theme(config, "simple-nord"));
    expect_colour(config, "background", "#2E3440");  /* themed */
    expect_colour(config, "text", "#FF00FF");        /* kept */
    expect_colour(config, "syntax_type", "#8FBCBB"); /* themed */

    CHECK(ase_config_is_chosen_by_hand(config, "text"));
    CHECK(!ase_config_is_chosen_by_hand(config, "background"));
    ase_config_destroy(config);
}

int main(void) {
    test_every_theme_is_complete();
    test_names_are_unique();
    test_lookup();
    test_applying_replaces_the_defaults();
    test_a_hand_set_colour_survives();
    test_a_config_full_of_shipped_defaults_still_themes();
    test_one_real_choice_among_defaults_survives();
    test_switching_themes_leaves_nothing_behind();
    test_hand_set_survives_switching();
    test_unknown_theme_changes_nothing();
    test_default_theme_matches_the_defaults();

    printf("all theme tests passed\n");
    return 0;
}
