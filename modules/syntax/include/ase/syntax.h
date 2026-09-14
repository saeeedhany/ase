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

/* Only C is supported in v1 (docs/adr/0007, decision 3). Returns NULL on
 * allocation failure. */
AseSyntax *ase_syntax_create_c(void);

void ase_syntax_destroy(AseSyntax *syntax);

typedef void (*AseHighlightCallback)(void *user_data, AseHighlightSpan span);

/* Parses `text` (len bytes) from scratch and invokes `callback` once per
 * highlight span found, in the order Tree-sitter's query engine returns
 * them (not guaranteed sorted by start offset — see docs/adr/0007,
 * decision 5, on why this isn't incremental). `text` need not be
 * NUL-terminated. */
/*
 * Spans for the bytes in [start_byte, end_byte) only.
 *
 * The parse is always of the whole text — a syntax tree of half a file
 * is not a syntax tree — but the *query* that turns the tree into spans
 * is the expensive half at scale, and an editor only ever draws a
 * screenful. Measured on a 10,800-line file: 64ms for the whole file
 * against roughly a millisecond for a window. See docs/adr/0072.
 *
 * Callers that pass the whole range get exactly what ase_syntax_highlight
 * gives them; it is now a wrapper around this.
 */
void ase_syntax_highlight_range(AseSyntax *syntax, const char *text, size_t len, size_t start_byte,
                                 size_t end_byte, AseHighlightCallback callback, void *user_data);

void ase_syntax_highlight(AseSyntax *syntax, const char *text, size_t len,
                           AseHighlightCallback callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* ASE_SYNTAX_H */
