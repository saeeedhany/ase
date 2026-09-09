#include "ase/syntax.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <tree_sitter/api.h>

#include "queries_embed.h"

extern const TSLanguage *tree_sitter_c(void);

struct AseSyntax {
    TSParser *parser;
    TSQuery *query;
    TSQueryCursor *cursor; /* reused across calls — ts_query_cursor_exec
                             * resets it for each new traversal, and
                             * this is on the keystroke hot path. */
    uint32_t capture_count;
    AseHighlightCapture *capture_map; /* indexed by query capture id */
};

static AseHighlightCapture capture_from_name(const char *name, uint32_t len) {
    if (len == 7 && memcmp(name, "keyword", 7) == 0) {
        return ASE_HL_KEYWORD;
    }
    if (len == 7 && memcmp(name, "comment", 7) == 0) {
        return ASE_HL_COMMENT;
    }
    if (len == 6 && memcmp(name, "string", 6) == 0) {
        return ASE_HL_STRING;
    }
    if (len == 6 && memcmp(name, "number", 6) == 0) {
        return ASE_HL_NUMBER;
    }
    if (len == 4 && memcmp(name, "type", 4) == 0) {
        return ASE_HL_TYPE;
    }
    return ASE_HL_NONE;
}

AseSyntax *ase_syntax_create_c(void) {
    AseSyntax *syntax = (AseSyntax *)calloc(1, sizeof(AseSyntax));
    if (syntax == NULL) {
        return NULL;
    }

    syntax->parser = ts_parser_new();
    if (syntax->parser == NULL) {
        free(syntax);
        return NULL;
    }

    const TSLanguage *language = tree_sitter_c();
    if (!ts_parser_set_language(syntax->parser, language)) {
        ts_parser_delete(syntax->parser);
        free(syntax);
        return NULL;
    }

    uint32_t error_offset;
    TSQueryError error_type;
    syntax->query = ts_query_new(language, ASE_C_HIGHLIGHTS_QUERY,
                                  (uint32_t)(sizeof(ASE_C_HIGHLIGHTS_QUERY) - 1),
                                  &error_offset, &error_type);
    if (syntax->query == NULL) {
        ts_parser_delete(syntax->parser);
        free(syntax);
        return NULL;
    }

    syntax->capture_count = ts_query_capture_count(syntax->query);
    syntax->capture_map = (AseHighlightCapture *)malloc(sizeof(AseHighlightCapture) * syntax->capture_count);
    if (syntax->capture_map == NULL) {
        ts_query_delete(syntax->query);
        ts_parser_delete(syntax->parser);
        free(syntax);
        return NULL;
    }

    for (uint32_t i = 0; i < syntax->capture_count; i++) {
        uint32_t name_len;
        const char *name = ts_query_capture_name_for_id(syntax->query, i, &name_len);
        syntax->capture_map[i] = capture_from_name(name, name_len);
    }

    syntax->cursor = ts_query_cursor_new();
    if (syntax->cursor == NULL) {
        free(syntax->capture_map);
        ts_query_delete(syntax->query);
        ts_parser_delete(syntax->parser);
        free(syntax);
        return NULL;
    }

    return syntax;
}

void ase_syntax_destroy(AseSyntax *syntax) {
    if (syntax == NULL) {
        return;
    }
    if (syntax->cursor != NULL) {
        ts_query_cursor_delete(syntax->cursor);
    }
    free(syntax->capture_map);
    if (syntax->query != NULL) {
        ts_query_delete(syntax->query);
    }
    if (syntax->parser != NULL) {
        ts_parser_delete(syntax->parser);
    }
    free(syntax);
}

void ase_syntax_highlight(AseSyntax *syntax, const char *text, size_t len,
                           AseHighlightCallback callback, void *user_data) {
    if (syntax == NULL || callback == NULL) {
        return;
    }

    TSTree *tree = ts_parser_parse_string(syntax->parser, NULL, text, (uint32_t)len);
    if (tree == NULL) {
        return;
    }

    TSNode root = ts_tree_root_node(tree);
    ts_query_cursor_exec(syntax->cursor, syntax->query, root);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(syntax->cursor, &match)) {
        for (uint16_t i = 0; i < match.capture_count; i++) {
            TSQueryCapture capture = match.captures[i];
            AseHighlightCapture mapped = syntax->capture_map[capture.index];
            if (mapped == ASE_HL_NONE) {
                continue;
            }

            AseHighlightSpan span;
            span.start = ts_node_start_byte(capture.node);
            span.end = ts_node_end_byte(capture.node);
            span.capture = mapped;
            callback(user_data, span);
        }
    }

    ts_tree_delete(tree);
}
