#ifndef ASE_THEME_H
#define ASE_THEME_H

#include <stdbool.h>
#include <stddef.h>

#include "ase/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A handful of palettes to start from, so the first thing someone does
 * is not pick nine colours.
 *
 * Each is six colours and no more, because this editor's aesthetic is
 * one background, one text colour, and two accents (ADR 0007, ADR 0048)
 * — a simplified Nord is Nord's ground, its text, and the two of its
 * sixteen colours that pair. Error and warning come from the palette
 * too, so a Nord editor does not flash an Atom-red underline. The rest
 * of the chrome is each palette's own selection colour rather than
 * something computed.
 */

typedef struct {
    const char *name;
    const char *summary;
    const char *background;
    const char *text;
    const char *selection;
    const char *find_match;
    const char *panel_background;
    const char *syntax_type;
    const char *syntax_string;
    const char *diagnostic_error;
    const char *diagnostic_warning;
} AseTheme;

size_t ase_theme_count(void);
const AseTheme *ase_theme_at(size_t index);
const AseTheme *ase_theme_find(const char *name);

/* Sets the theme's colours, leaving alone anything the user set by hand
 * in a config file. False when no theme has that name.
 *
 * The layering is what makes `:theme` honest: previewing a theme and
 * saving it produce the same screen, because both go through here. */
bool ase_config_apply_theme(AseConfig *config, const char *name);

#ifdef __cplusplus
}
#endif

#endif
