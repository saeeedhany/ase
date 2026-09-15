/*
 * libFuzzer harness for the Tree-sitter-backed syntax module — feeds
 * arbitrary bytes straight to the C parser, exactly the "parser
 * boundary" fuzz target called for in spec section 5. One AseSyntax is
 * created once (LLVMFuzzerInitialize) and reused across iterations so
 * query compilation doesn't dominate fuzzing throughput.
 *
 * Build (clang only): cmake -B build -DASE_BUILD_FUZZERS=ON -DCMAKE_C_COMPILER=clang
 * Run:                ./build/modules/syntax/fuzz/ase_syntax_fuzz
 */

#include <stddef.h>
#include <stdint.h>

#include "ase/syntax.h"

static AseSyntax *g_syntax = NULL;

static void discard_span(void *user_data, AseHighlightSpan span) {
    (void)user_data;
    (void)span;
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    (void)argv;
    g_syntax = ase_syntax_create(ASE_LANG_C);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (g_syntax == NULL) {
        return 0;
    }
    ase_syntax_highlight(g_syntax, (const char *)data, size, discard_span, NULL);
    return 0;
}
