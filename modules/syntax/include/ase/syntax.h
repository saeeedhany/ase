#ifndef ASE_SYNTAX_H
#define ASE_SYNTAX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tree-sitter-backed syntax highlighting. See
 * docs/adr/0007-syntax-highlighting-tree-sitter.md for why the capture
 * set is this small and why there's only one language in v1. The
 * default theme originally rendered every capture as a shade of one
 * color (docs/adr/0007's "one font color" pillar); docs/adr/0048 later
 * added two real accent colors (ASE_HL_TYPE, ASE_HL_STRING) as a
 * deliberate, deliberately small departure from that — everything else
 * here still varies only by weight/opacity, not hue.
 */

typedef enum {
    ASE_HL_NONE = 0,
    ASE_HL_KEYWORD,
    ASE_HL_COMMENT,
    ASE_HL_STRING,
    ASE_HL_NUMBER,
    ASE_HL_TYPE,
} AseHighlightCapture;

typedef struct {
    size_t start;
    size_t end;
    AseHighlightCapture capture;
} AseHighlightSpan;

typedef struct AseSyntax AseSyntax;

typedef enum {
    ASE_LANG_C,
    ASE_LANG_CPP,
    ASE_LANG_PYTHON,
    ASE_LANG_JAVASCRIPT,
    ASE_LANG_CSS,
    ASE_LANG_HTML,
    ASE_LANG_LUA,
} AseLanguage;

/* NULL on allocation failure or an unknown language. Each language costs
 * a compiled-in grammar, so the set stays short — see docs/adr/0085. */
AseSyntax *ase_syntax_create(AseLanguage language);

void ase_syntax_destroy(AseSyntax *syntax);

typedef void (*AseHighlightCallback)(void *user_data, AseHighlightSpan span);

/*
 * Spans for the bytes in [start_byte, end_byte) only, in whatever order
 * the query engine returns them. `text` need not be NUL-terminated.
 *
 * The parse always covers the whole text; only the query is windowed.
 * See docs/adr/0072.
 *
 * `syntax` is stateful: it keeps the last tree and a copy of the text it
 * was parsed from, so it costs memory per handle and is only incremental
 * when successive texts are related. One handle per document.
 */
void ase_syntax_highlight_range(AseSyntax *syntax, const char *text, size_t len, size_t start_byte,
                                 size_t end_byte, AseHighlightCallback callback, void *user_data);

/* ase_syntax_highlight_range over [0, len). */
void ase_syntax_highlight(AseSyntax *syntax, const char *text, size_t len,
                           AseHighlightCallback callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* ASE_SYNTAX_H */
