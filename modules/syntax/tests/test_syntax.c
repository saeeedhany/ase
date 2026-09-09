#include <assert.h>
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
        assert(list->spans != NULL);
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
    assert(syntax != NULL);

    const char *source =
        "// greeting\n"
        "int main(void) {\n"
        "    const char *msg = \"hello\";\n"
        "    int x = 42;\n"
        "    return 0;\n"
        "}\n";

    SpanList list = {0};
    ase_syntax_highlight(syntax, source, strlen(source), collect, &list);

    assert(list.count > 0);
    assert(has_span(&list, source, ASE_HL_COMMENT, "// greeting"));
    assert(has_span(&list, source, ASE_HL_KEYWORD, "const"));
    assert(has_span(&list, source, ASE_HL_KEYWORD, "return"));
    assert(has_span(&list, source, ASE_HL_TYPE, "int"));
    assert(has_span(&list, source, ASE_HL_TYPE, "char"));
    assert(has_span(&list, source, ASE_HL_STRING, "\"hello\""));
    assert(has_span(&list, source, ASE_HL_NUMBER, "42"));
    assert(has_span(&list, source, ASE_HL_NUMBER, "0"));

    free(list.spans);
    ase_syntax_destroy(syntax);

    printf("all syntax tests passed\n");
    return 0;
}
