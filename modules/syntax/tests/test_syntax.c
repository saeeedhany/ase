#include "../../../core/tests/test_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ase/syntax.h"

typedef struct {
    AseHighlightSpan *spans;
    size_t count;
    size_t capacity;
} SpanList;

static void collect(void *user_data, AseHighlightSpan span) {
    SpanList *list = (SpanList *)user_data;
    if (list->count == list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->spans = (AseHighlightSpan *)realloc(list->spans, list->capacity * sizeof(AseHighlightSpan));
        CHECK(list->spans != NULL);
    }
    list->spans[list->count++] = span;
}

static int has_span(const SpanList *list, const char *text, AseHighlightCapture capture, const char *expected) {
    size_t expected_len = strlen(expected);
    for (size_t i = 0; i < list->count; i++) {
        AseHighlightSpan span = list->spans[i];
        if (span.capture != capture) {
            continue;
        }
        if (span.end - span.start != expected_len) {
            continue;
        }
        if (memcmp(text + span.start, expected, expected_len) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    AseSyntax *syntax = ase_syntax_create_c();
    CHECK(syntax != NULL);

    const char *source =
        "// greeting\n"
        "int main(void) {\n"
        "    const char *msg = \"hello\";\n"
        "    int x = 42;\n"
        "    return 0;\n"
        "}\n";

    SpanList list = {0};
    ase_syntax_highlight(syntax, source, strlen(source), collect, &list);

    CHECK(list.count > 0);
    CHECK(has_span(&list, source, ASE_HL_COMMENT, "// greeting"));
    CHECK(has_span(&list, source, ASE_HL_KEYWORD, "const"));
    CHECK(has_span(&list, source, ASE_HL_KEYWORD, "return"));
    CHECK(has_span(&list, source, ASE_HL_TYPE, "int"));
    CHECK(has_span(&list, source, ASE_HL_TYPE, "char"));
    CHECK(has_span(&list, source, ASE_HL_STRING, "\"hello\""));
    CHECK(has_span(&list, source, ASE_HL_NUMBER, "42"));
    CHECK(has_span(&list, source, ASE_HL_NUMBER, "0"));

    free(list.spans);
    ase_syntax_destroy(syntax);

    printf("all syntax tests passed\n");
    return 0;
}
