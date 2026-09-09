#ifndef ASE_SYNTAX_H
#define ASE_SYNTAX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tree-sitter-backed syntax highlighting. See
 * docs/adr/0007-syntax-highlighting-tree-sitter.md for why the capture
 * set is this small, why there's only one language in v1, and why the
 * default theme renders all of these as shades of one color rather than
 * distinct hues.
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
void ase_syntax_highlight(AseSyntax *syntax, const char *text, size_t len,
                           AseHighlightCallback callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* ASE_SYNTAX_H */
