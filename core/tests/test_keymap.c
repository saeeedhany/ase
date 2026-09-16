#include "test_assert.h"
#include <stdio.h>
#include <string.h>

#include "ase/keymap.h"

static void expect(const char *input, const char *want) {
    char out[64];
    if (!ase_keymap_canonical(input, out, sizeof(out))) {
        printf("'%s' was rejected, expected '%s'\n", input, want);
        CHECK(0);
        return;
    }
    if (strcmp(out, want) != 0) {
        printf("'%s' became '%s', expected '%s'\n", input, out, want);
        CHECK(0);
    }
}

static void expect_rejected(const char *input) {
    char out[64];
    if (ase_keymap_canonical(input, out, sizeof(out))) {
        printf("'%s' was accepted as '%s', expected a rejection\n", input, out);
        CHECK(0);
    }
}

/* The whole point: three spellings of one chord have to compare equal. */
static void test_spellings_agree(void) {
    expect("Ctrl+Shift+F", "ctrl+shift+f");
    expect("shift+ctrl+f", "ctrl+shift+f");
    expect("CTRL+SHIFT+F", "ctrl+shift+f");
    expect("  ctrl + shift + f  ", "ctrl+shift+f");
    expect("Control+Shift+F", "ctrl+shift+f");
}

static void test_modifier_order_is_fixed(void) {
    expect("shift+alt+ctrl+p", "ctrl+alt+shift+p");
    expect("alt+ctrl+o", "ctrl+alt+o");
    expect("meta+ctrl+s", "ctrl+meta+s");
}

static void test_plain_keys(void) {
    expect("f", "f");
    expect("F", "f");
    expect("f1", "f1");
    expect("F12", "f12");
    expect("Escape", "escape");
    expect("Tab", "tab");
    expect("Space", "space");
    expect("Return", "return");
    expect("0", "0");
}

/* '+' is both the separator and a key someone may want to bind. */
static void test_the_plus_key(void) {
    expect("plus", "plus");
    expect("ctrl+plus", "ctrl+plus");
    expect("ctrl++", "ctrl+plus");
    /* Bare '+' is the key, not a dangling separator. */
    expect("+", "plus");
}

static void test_punctuation_keys(void) {
    expect("ctrl+semicolon", "ctrl+semicolon");
    expect("ctrl+;", "ctrl+semicolon");
    expect("ctrl+=", "ctrl+equal");
    expect("ctrl+-", "ctrl+minus");
    expect("ctrl+/", "ctrl+slash");
}

/* A typo must not bind nothing in silence. */
static void test_rejections(void) {
    expect_rejected("");
    expect_rejected("ctrl+");
    /* A trailing separator after a key: "ctrl+" alone is caught by
     * having no key at all, this one is not. */
    expect_rejected("ctrl+f+");
    expect_rejected("f+");
    expect_rejected("ctrl");            /* a modifier is not a chord */
    expect_rejected("shift");
    expect_rejected("ctrl+nosuchkey");
    expect_rejected("ctrl+f13");        /* only f1..f12 exist */
    expect_rejected("ctrl+ctrl+f");     /* a modifier twice */
    expect_rejected("ctrl+f+g");        /* two keys */
    expect_rejected("ctrl shift f");    /* spaces are not separators */
}

static void test_null_and_small_buffers(void) {
    char out[64];
    CHECK(!ase_keymap_canonical(NULL, out, sizeof(out)));
    CHECK(!ase_keymap_canonical("ctrl+f", NULL, sizeof(out)));
    /* "ctrl+shift+f" needs 13 bytes; 8 cannot hold it. */
    char small[8];
    CHECK(!ase_keymap_canonical("ctrl+shift+f", small, sizeof(small)));
}

static void test_format_from_parts(void) {
    char out[64];
    CHECK(ase_keymap_format(true, false, true, false, "f", out, sizeof(out)));
    CHECK(strcmp(out, "ctrl+shift+f") == 0);
    CHECK(ase_keymap_format(false, false, false, false, "f1", out, sizeof(out)));
    CHECK(strcmp(out, "f1") == 0);
    CHECK(ase_keymap_format(false, true, false, false, "o", out, sizeof(out)));
    CHECK(strcmp(out, "alt+o") == 0);
    /* An unknown key name is rejected here too. */
    CHECK(!ase_keymap_format(true, false, false, false, "nosuchkey", out, sizeof(out)));
}

/* Round trip: anything format() produces, canonical() must accept
 * unchanged, or the two halves disagree and a binding never matches. */
static void test_format_output_is_already_canonical(void) {
    static const char *keys[] = {"a", "z", "0", "9", "f1", "f12", "escape", "tab",
                                 "space", "return", "semicolon", "plus", "left"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        for (int bits = 0; bits < 16; bits++) {
            char built[64];
            bool ok = ase_keymap_format(bits & 1, bits & 2, bits & 4, bits & 8, keys[i], built,
                                         sizeof(built));
            CHECK(ok);
            char again[64];
            if (!ase_keymap_canonical(built, again, sizeof(again))) {
                printf("format produced '%s', which canonical rejects\n", built);
                CHECK(0);
                continue;
            }
            if (strcmp(built, again) != 0) {
                printf("'%s' canonicalises to '%s'\n", built, again);
                CHECK(0);
            }
        }
    }
}

static void test_is_known_key(void) {
    CHECK(ase_keymap_is_known_key("f"));
    CHECK(ase_keymap_is_known_key("f12"));
    CHECK(ase_keymap_is_known_key("escape"));
    CHECK(!ase_keymap_is_known_key("ctrl"));
    CHECK(!ase_keymap_is_known_key("nosuchkey"));
    CHECK(!ase_keymap_is_known_key(NULL));
}

int main(void) {
    test_spellings_agree();
    test_modifier_order_is_fixed();
    test_plain_keys();
    test_the_plus_key();
    test_punctuation_keys();
    test_rejections();
    test_null_and_small_buffers();
    test_format_from_parts();
    test_format_output_is_already_canonical();
    test_is_known_key();

    printf("all keymap tests passed\n");
    return 0;
}
