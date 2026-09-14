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

static int span_cmp(const void *a, const void *b) {
    const AseHighlightSpan *x = (const AseHighlightSpan *)a;
    const AseHighlightSpan *y = (const AseHighlightSpan *)b;
    if (x->start != y->start) {
        return x->start < y->start ? -1 : 1;
    }
    if (x->end != y->end) {
        return x->end < y->end ? -1 : 1;
    }
    return (int)x->capture - (int)y->capture;
}

static void highlight_into(AseSyntax *syntax, const char *text, SpanList *list) {
    list->count = 0;
    ase_syntax_highlight(syntax, text, strlen(text), collect, list);
    if (list->count > 1) {
        /* Query order isn't guaranteed sorted, so compare as sets. */
        qsort(list->spans, list->count, sizeof(AseHighlightSpan), span_cmp);
    }
}

/* A bad TSInputEdit still yields a tree, just with subtly wrong spans.
 * So: `before` then `after` on a reused handle must equal `after` on a
 * fresh one. */
static void check_incremental_matches_fresh(const char *label, const char *before, const char *after) {
    AseSyntax *reused = ase_syntax_create_c();
    AseSyntax *fresh = ase_syntax_create_c();
    CHECK(reused != NULL);
    CHECK(fresh != NULL);

    SpanList warmup = {0};
    SpanList incremental = {0};
    SpanList expected = {0};

    highlight_into(reused, before, &warmup);
    highlight_into(reused, after, &incremental);
    highlight_into(fresh, after, &expected);

    if (incremental.count != expected.count) {
        fprintf(stderr, "%s: %zu spans incrementally, %zu from scratch\n", label, incremental.count,
                expected.count);
    }
    CHECK(incremental.count == expected.count);
    for (size_t i = 0; i < expected.count; i++) {
        AseHighlightSpan got = incremental.spans[i];
        AseHighlightSpan want = expected.spans[i];
        if (got.start != want.start || got.end != want.end || got.capture != want.capture) {
            fprintf(stderr, "%s: span %zu was [%zu,%zu) capture %d, expected [%zu,%zu) capture %d\n", label,
                    i, got.start, got.end, (int)got.capture, want.start, want.end, (int)want.capture);
        }
        CHECK(got.start == want.start);
        CHECK(got.end == want.end);
        CHECK(got.capture == want.capture);
    }

    free(warmup.spans);
    free(incremental.spans);
    free(expected.spans);
    ase_syntax_destroy(reused);
    ase_syntax_destroy(fresh);
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

    const char *base =
        "// greeting\n"
        "int main(void) {\n"
        "    const char *msg = \"hello\";\n"
        "    int x = 42;\n"
        "    return 0;\n"
        "}\n";

    check_incremental_matches_fresh("unchanged", base, base);
    check_incremental_matches_fresh("insert at start", base, "#include <stdio.h>\n// greeting\n");
    check_incremental_matches_fresh("insert in middle", base,
                                     "// greeting\n"
                                     "int main(void) {\n"
                                     "    unsigned long n = 7;\n"
                                     "    const char *msg = \"hello\";\n"
                                     "    int x = 42;\n"
                                     "    return 0;\n"
                                     "}\n");
    check_incremental_matches_fresh("insert at end", base,
                                     "// greeting\n"
                                     "int main(void) {\n"
                                     "    const char *msg = \"hello\";\n"
                                     "    int x = 42;\n"
                                     "    return 0;\n"
                                     "}\n"
                                     "static int helper(void) { return 1; }\n");
    check_incremental_matches_fresh("delete at start", base,
                                     "int main(void) {\n"
                                     "    const char *msg = \"hello\";\n"
                                     "    int x = 42;\n"
                                     "    return 0;\n"
                                     "}\n");
    check_incremental_matches_fresh("delete in middle", base,
                                     "// greeting\n"
                                     "int main(void) {\n"
                                     "    int x = 42;\n"
                                     "    return 0;\n"
                                     "}\n");
    check_incremental_matches_fresh("delete at end", base,
                                     "// greeting\n"
                                     "int main(void) {\n"
                                     "    const char *msg = \"hello\";\n"
                                     "    int x = 42;\n");
    /* Same length: prefix and suffix meet with nothing between them. */
    check_incremental_matches_fresh("replace same length", base,
                                     "// greeting\n"
                                     "int main(void) {\n"
                                     "    const char *msg = \"world\";\n"
                                     "    int x = 42;\n"
                                     "    return 0;\n"
                                     "}\n");
    check_incremental_matches_fresh("one keystroke", base,
                                     "// greeting\n"
                                     "int main(void) {\n"
                                     "    const char *msg = \"hello\";\n"
                                     "    int xy = 42;\n"
                                     "    return 0;\n"
                                     "}\n");
    check_incremental_matches_fresh("comment opened", base,
                                     "// greeting\n"
                                     "int main(void) { /*\n"
                                     "    const char *msg = \"hello\";\n"
                                     "    int x = 42;\n"
                                     "    return 0;\n"
                                     "}\n");
    check_incremental_matches_fresh("emptied", base, "");
    check_incremental_matches_fresh("grown from empty", "", base);

    printf("all syntax tests passed\n");
    return 0;
}
