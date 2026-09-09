#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/json.h"

static AseJsonValue *parse(const char *text) {
    return ase_json_parse(text, strlen(text));
}

static void test_parse_primitives(void) {
    AseJsonValue *v;

    v = parse("null");
    assert(v != NULL && ase_json_type(v) == ASE_JSON_NULL);
    ase_json_destroy(v);

    v = parse("true");
    assert(v != NULL && ase_json_get_bool(v, false) == true);
    ase_json_destroy(v);

    v = parse("false");
    assert(v != NULL && ase_json_get_bool(v, true) == false);
    ase_json_destroy(v);

    v = parse("42");
    assert(v != NULL && ase_json_get_number(v, -1) == 42.0);
    ase_json_destroy(v);

    v = parse("-3.5");
    assert(v != NULL && ase_json_get_number(v, 0) == -3.5);
    ase_json_destroy(v);

    v = parse("1.5e2");
    assert(v != NULL && ase_json_get_number(v, 0) == 150.0);
    ase_json_destroy(v);

    v = parse("\"hello\"");
    assert(v != NULL && strcmp(ase_json_get_string(v), "hello") == 0);
    ase_json_destroy(v);
}

static void test_whitespace_tolerance(void) {
    AseJsonValue *v = parse("  \n\t {  \"a\"  :  1  }  \n");
    assert(v != NULL);
    assert(ase_json_get_number(ase_json_object_get(v, "a"), -1) == 1.0);
    ase_json_destroy(v);
}

static void test_string_escapes(void) {
    AseJsonValue *v = parse("\"a\\\"b\\\\c\\nd\\te\"");
    assert(v != NULL);
    assert(strcmp(ase_json_get_string(v), "a\"b\\c\nd\te") == 0);
    ase_json_destroy(v);
}

static void test_unicode_escapes(void) {
    /* é = 'e' with acute accent, U+00E9, UTF-8: 0xC3 0xA9 */
    AseJsonValue *v = parse("\"caf\\u00e9\"");
    assert(v != NULL);
    const char *s = ase_json_get_string(v);
    assert(strlen(s) == 5); /* c a f + 2-byte utf8 */
    assert((unsigned char)s[3] == 0xC3 && (unsigned char)s[4] == 0xA9);
    ase_json_destroy(v);
}

static void test_surrogate_pair(void) {
    /* U+1F600 (grinning face emoji) as a UTF-16 surrogate pair:
     * high 0xD83D, low 0xDE00 -> UTF-8: F0 9F 98 80 */
    AseJsonValue *v = parse("\"\\ud83d\\ude00\"");
    assert(v != NULL);
    const char *s = ase_json_get_string(v);
    assert(strlen(s) == 4);
    assert((unsigned char)s[0] == 0xF0 && (unsigned char)s[1] == 0x9F);
    assert((unsigned char)s[2] == 0x98 && (unsigned char)s[3] == 0x80);
    ase_json_destroy(v);
}

static void test_nested_structure(void) {
    const char *text =
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"items\":[1,2,3],\"ok\":true,\"note\":null}}";
    AseJsonValue *v = parse(text);
    assert(v != NULL);
    assert(ase_json_type(v) == ASE_JSON_OBJECT);
    assert(strcmp(ase_json_get_string(ase_json_object_get(v, "jsonrpc")), "2.0") == 0);
    assert(ase_json_get_number(ase_json_object_get(v, "id"), -1) == 7.0);

    AseJsonValue *result = ase_json_object_get(v, "result");
    assert(result != NULL);
    AseJsonValue *items = ase_json_object_get(result, "items");
    assert(ase_json_array_size(items) == 3);
    assert(ase_json_get_number(ase_json_array_get(items, 0), -1) == 1.0);
    assert(ase_json_get_number(ase_json_array_get(items, 2), -1) == 3.0);
    assert(ase_json_get_bool(ase_json_object_get(result, "ok"), false) == true);
    assert(ase_json_type(ase_json_object_get(result, "note")) == ASE_JSON_NULL);

    assert(ase_json_object_get(v, "no_such_key") == NULL);
    assert(ase_json_array_get(items, 99) == NULL);

    ase_json_destroy(v);
}

static void test_malformed_input_rejected(void) {
    const char *bad_inputs[] = {
        "",
        "{",
        "}",
        "[1,2,",
        "{\"a\":}",
        "{\"a\" 1}",
        "\"unterminated",
        "\"bad\\xescape\"",
        "tru",
        "nul",
        "1 2",    /* trailing garbage after a valid top-level value */
        "{\"a\":1}garbage",
    };
    for (size_t i = 0; i < sizeof(bad_inputs) / sizeof(bad_inputs[0]); i++) {
        AseJsonValue *v = parse(bad_inputs[i]);
        assert(v == NULL);
    }
}

static void test_builder_and_write_round_trip(void) {
    AseJsonValue *obj = ase_json_object();
    assert(ase_json_object_set(obj, "name", ase_json_string("ase")));
    assert(ase_json_object_set(obj, "version", ase_json_number(1)));
    assert(ase_json_object_set(obj, "ok", ase_json_bool(true)));
    assert(ase_json_object_set(obj, "extra", ase_json_null()));

    AseJsonValue *arr = ase_json_array();
    assert(ase_json_array_append(arr, ase_json_number(1)));
    assert(ase_json_array_append(arr, ase_json_number(2)));
    assert(ase_json_object_set(obj, "items", arr));

    /* replacing an existing key must not grow the object or leak the old value */
    assert(ase_json_object_set(obj, "version", ase_json_number(2)));

    char *text = ase_json_write(obj);
    assert(text != NULL);

    AseJsonValue *reparsed = ase_json_parse(text, strlen(text));
    assert(reparsed != NULL);
    assert(strcmp(ase_json_get_string(ase_json_object_get(reparsed, "name")), "ase") == 0);
    assert(ase_json_get_number(ase_json_object_get(reparsed, "version"), -1) == 2.0);
    assert(ase_json_get_bool(ase_json_object_get(reparsed, "ok"), false) == true);
    assert(ase_json_array_size(ase_json_object_get(reparsed, "items")) == 2);

    free(text);
    ase_json_destroy(reparsed);
    ase_json_destroy(obj);
}

static void test_write_escapes_control_and_quotes(void) {
    AseJsonValue *v = ase_json_string("line1\nline2\t\"quoted\"\\backslash");
    char *text = ase_json_write(v);
    assert(text != NULL);

    AseJsonValue *reparsed = ase_json_parse(text, strlen(text));
    assert(reparsed != NULL);
    assert(strcmp(ase_json_get_string(reparsed), "line1\nline2\t\"quoted\"\\backslash") == 0);

    free(text);
    ase_json_destroy(reparsed);
    ase_json_destroy(v);
}

static void test_destroy_null_is_safe(void) {
    ase_json_destroy(NULL);
}

static void test_ownership_on_failed_append(void) {
    /* appending into a non-array/non-object must destroy the item rather
     * than leak it (verified for real by running this file under ASan). */
    AseJsonValue *not_an_array = ase_json_number(1);
    assert(!ase_json_array_append(not_an_array, ase_json_number(2)));

    AseJsonValue *not_an_object = ase_json_number(1);
    assert(!ase_json_object_set(not_an_object, "k", ase_json_number(2)));

    ase_json_destroy(not_an_array);
    ase_json_destroy(not_an_object);
}

int main(void) {
    test_parse_primitives();
    test_whitespace_tolerance();
    test_string_escapes();
    test_unicode_escapes();
    test_surrogate_pair();
    test_nested_structure();
    test_malformed_input_rejected();
    test_builder_and_write_round_trip();
    test_write_escapes_control_and_quotes();
    test_destroy_null_is_safe();
    test_ownership_on_failed_append();

    printf("all json tests passed\n");
    return 0;
}
