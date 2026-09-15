#include "ase/syntax.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <tree_sitter/api.h>

#include "queries_embed.h"

extern const TSLanguage *tree_sitter_c(void);
extern const TSLanguage *tree_sitter_cpp(void);
extern const TSLanguage *tree_sitter_python(void);
extern const TSLanguage *tree_sitter_javascript(void);
extern const TSLanguage *tree_sitter_css(void);
extern const TSLanguage *tree_sitter_html(void);
extern const TSLanguage *tree_sitter_lua(void);

struct AseSyntax {
    TSParser *parser;
    TSQuery *query;
    TSQueryCursor *cursor; /* reused across calls — ts_query_cursor_exec
                             * resets it for each new traversal, and
                             * this is on the keystroke hot path. */
    uint32_t capture_count;
    AseHighlightCapture *capture_map; /* indexed by query capture id */

    /* The last parse, kept so the next one can be incremental — see
     * docs/adr/0072. `text` is a copy of exactly what `tree` was parsed
     * from, which is what lets the edit between then and now be derived
     * here instead of threaded through every caller. */
    TSTree *tree;
    char *text;
    size_t text_len;
};

/* Advances `point` over text[from, to), which is how the three points a
 * TSInputEdit needs get computed with one pass over the file instead of
 * three: the first point is found from the start, and the other two —
 * both at or after it — continue from there. */
static TSPoint advance_point(TSPoint point, const char *text, size_t from, size_t to) {
    for (size_t i = from; i < to; i++) {
        if (text[i] == '\n') {
            point.row++;
            point.column = 0;
        } else {
            point.column++;
        }
    }
    return point;
}

/*
 * Describes the difference between the stored text and the new text as
 * one replaced span: trim the common prefix and the common suffix, and
 * whatever is left in the middle is "the edit".
 *
 * Deliberately not an exact edit list. Several separate changes (a
 * multi-cursor edit, an undo restoring scattered text) collapse into a
 * single span covering all of them, which is *correct* — a TSInputEdit
 * may describe more than actually changed, it only costs a larger
 * reparse — and it keeps ase_syntax_highlight()'s "here is the whole
 * text" contract, so no caller has to track edits.
 */
static bool derive_edit(const char *old_text, size_t old_len, const char *new_text, size_t new_len,
                         TSInputEdit *edit) {
    size_t shorter = old_len < new_len ? old_len : new_len;
    size_t prefix = 0;
    while (prefix < shorter && old_text[prefix] == new_text[prefix]) {
        prefix++;
    }
    if (prefix == old_len && old_len == new_len) {
        return false; /* identical: the stored tree is still exact */
    }

    size_t suffix = 0;
    size_t max_suffix = shorter - prefix;
    while (suffix < max_suffix && old_text[old_len - 1 - suffix] == new_text[new_len - 1 - suffix]) {
        suffix++;
    }

    TSPoint start_point = advance_point((TSPoint){0, 0}, old_text, 0, prefix);
    edit->start_byte = (uint32_t)prefix;
    edit->old_end_byte = (uint32_t)(old_len - suffix);
    edit->new_end_byte = (uint32_t)(new_len - suffix);
    edit->start_point = start_point;
    edit->old_end_point = advance_point(start_point, old_text, prefix, old_len - suffix);
    edit->new_end_point = advance_point(start_point, new_text, prefix, new_len - suffix);
    return true;
}

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

static AseSyntax *syntax_create(const TSLanguage *language, const char *query_src, size_t query_len) {
    AseSyntax *syntax = (AseSyntax *)calloc(1, sizeof(AseSyntax));
    if (syntax == NULL) {
        return NULL;
    }

    syntax->parser = ts_parser_new();
    if (syntax->parser == NULL) {
        free(syntax);
        return NULL;
    }

    if (!ts_parser_set_language(syntax->parser, language)) {
        ts_parser_delete(syntax->parser);
        free(syntax);
        return NULL;
    }

    uint32_t error_offset;
    TSQueryError error_type;
    syntax->query = ts_query_new(language, query_src, (uint32_t)query_len, &error_offset, &error_type);
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

AseSyntax *ase_syntax_create(AseLanguage language) {
    switch (language) {
    case ASE_LANG_C:
        return syntax_create(tree_sitter_c(), ASE_C_HIGHLIGHTS_QUERY, sizeof(ASE_C_HIGHLIGHTS_QUERY) - 1);
    case ASE_LANG_CPP:
        return syntax_create(tree_sitter_cpp(), ASE_CPP_HIGHLIGHTS_QUERY,
                              sizeof(ASE_CPP_HIGHLIGHTS_QUERY) - 1);
    case ASE_LANG_PYTHON:
        return syntax_create(tree_sitter_python(), ASE_PYTHON_HIGHLIGHTS_QUERY,
                              sizeof(ASE_PYTHON_HIGHLIGHTS_QUERY) - 1);
    case ASE_LANG_JAVASCRIPT:
        return syntax_create(tree_sitter_javascript(), ASE_JAVASCRIPT_HIGHLIGHTS_QUERY,
                              sizeof(ASE_JAVASCRIPT_HIGHLIGHTS_QUERY) - 1);
    case ASE_LANG_CSS:
        return syntax_create(tree_sitter_css(), ASE_CSS_HIGHLIGHTS_QUERY,
                              sizeof(ASE_CSS_HIGHLIGHTS_QUERY) - 1);
    case ASE_LANG_HTML:
        return syntax_create(tree_sitter_html(), ASE_HTML_HIGHLIGHTS_QUERY,
                              sizeof(ASE_HTML_HIGHLIGHTS_QUERY) - 1);
    case ASE_LANG_LUA:
        return syntax_create(tree_sitter_lua(), ASE_LUA_HIGHLIGHTS_QUERY,
                              sizeof(ASE_LUA_HIGHLIGHTS_QUERY) - 1);
    }
    return NULL;
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
    if (syntax->tree != NULL) {
        ts_tree_delete(syntax->tree);
    }
    free(syntax->text);
    free(syntax);
}

void ase_syntax_highlight(AseSyntax *syntax, const char *text, size_t len,
                           AseHighlightCallback callback, void *user_data) {
    ase_syntax_highlight_range(syntax, text, len, 0, len, callback, user_data);
}

static void run_query(AseSyntax *syntax, TSTree *tree, size_t start_byte, size_t end_byte,
                       AseHighlightCallback callback, void *user_data) {
    TSNode root = ts_tree_root_node(tree);
    /* Set every call: the cursor is reused (see the struct), so a range
     * left over from a previous window would silently clip this one. */
    ts_query_cursor_set_byte_range(syntax->cursor, (uint32_t)start_byte, (uint32_t)end_byte);
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
}

void ase_syntax_highlight_range(AseSyntax *syntax, const char *text, size_t len, size_t start_byte,
                                 size_t end_byte, AseHighlightCallback callback, void *user_data) {
    if (syntax == NULL || callback == NULL) {
        return;
    }

    /*
     * Incremental where it can be: the previous tree is kept, told what
     * changed, and handed back to the parser, which then re-parses only
     * the affected subtree rather than the whole file. Measured on a
     * 10,800-line file: 107ms -> 3.4ms for one keystroke.
     *
     * Falls back to a full parse whenever there is nothing to be
     * incremental *from*. A wrong incremental parse corrupts
     * highlighting in ways that look like a Tree-sitter bug, so any path
     * that cannot prove the stored text matches the stored tree throws
     * both away. See docs/adr/0072.
     *
     * Invariant: syntax->tree is non-NULL only when syntax->text holds
     * the bytes it was parsed from.
     */
    TSTree *old_tree = NULL;
    if (syntax->tree != NULL && syntax->text != NULL) {
        TSInputEdit edit;
        if (!derive_edit(syntax->text, syntax->text_len, text, len, &edit)) {
            /* Identical text: the tree is still exact. Scrolling
             * re-enters here at frame rate, so skipping the re-copy and
             * the parser round-trip matters. */
            run_query(syntax, syntax->tree, start_byte, end_byte, callback, user_data);
            return;
        }
        ts_tree_edit(syntax->tree, &edit);
        old_tree = syntax->tree;
        syntax->tree = NULL;
    }

    TSTree *tree = ts_parser_parse_string(syntax->parser, old_tree, text, (uint32_t)len);
    if (old_tree != NULL) {
        ts_tree_delete(old_tree);
    }
    if (tree == NULL) {
        free(syntax->text);
        syntax->text = NULL;
        syntax->text_len = 0;
        return;
    }

    /* Remember exactly what this tree was parsed from. */
    char *copy = (char *)malloc(len > 0 ? len : 1);
    if (copy != NULL) {
        memcpy(copy, text, len);
    }
    free(syntax->text);
    syntax->text = copy;
    syntax->text_len = (copy != NULL) ? len : 0;

    run_query(syntax, tree, start_byte, end_byte, callback, user_data);

    /* A tree stored without its text fails the guard above next call,
     * which would overwrite it without freeing: one leak per call. */
    if (copy != NULL) {
        syntax->tree = tree;
    } else {
        ts_tree_delete(tree);
        syntax->tree = NULL;
    }
}
