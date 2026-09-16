#include "ase/theme.h"

#include "internal.h"

#include <string.h>

/*
 * Nord and Solarized each define sixteen colours. Taking all sixteen
 * would make this editor look like every other one that ships them;
 * taking the ground, the text, and the two that pair is what keeps it
 * looking like itself in someone else's palette.
 *
 * Solarized is dark only. The panel alpha, the dimmed comment tier and
 * the gutter are all tuned for a dark ground, and a light theme is a
 * second set of rules rather than a second row here.
 */
static const AseTheme kThemes[] = {
    {
        "ase-default",
        "Warm cream on near-black",
        "#282828", "#F5E6C8", "#45403866", "#45403899", "#282828E6",
        "#689D6A", "#D79921", "#E06C75", "#E5C07B",
    },
    {
        "simple-nord",
        "Nord's Polar Night and Snow Storm, with one Frost and one Aurora",
        "#2E3440", "#D8DEE9", "#434C5E66", "#434C5E99", "#2E3440E6",
        "#8FBCBB", "#EBCB8B", "#BF616A", "#EBCB8B",
    },
    {
        "simple-sola",
        "Solarized Dark, down to base03, base1, cyan and yellow",
        "#002B36", "#93A1A1", "#07364266", "#07364299", "#002B36E6",
        "#2AA198", "#B58900", "#DC322F", "#B58900",
    },
};

size_t ase_theme_count(void) {
    return sizeof(kThemes) / sizeof(kThemes[0]);
}

const AseTheme *ase_theme_at(size_t index) {
    return index < ase_theme_count() ? &kThemes[index] : NULL;
}

const AseTheme *ase_theme_find(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < ase_theme_count(); i++) {
        if (strcmp(kThemes[i].name, name) == 0) {
            return &kThemes[i];
        }
    }
    return NULL;
}

bool ase_config_apply_theme(AseConfig *config, const char *name) {
    const AseTheme *theme = ase_theme_find(name);
    if (config == NULL || theme == NULL) {
        return false;
    }
    const struct {
        const char *key;
        const char *value;
    } colours[] = {
        {"background", theme->background},
        {"text", theme->text},
        {"selection", theme->selection},
        {"find_match", theme->find_match},
        {"panel_background", theme->panel_background},
        {"syntax_type", theme->syntax_type},
        {"syntax_string", theme->syntax_string},
        {"diagnostic_error", theme->diagnostic_error},
        {"diagnostic_warning", theme->diagnostic_warning},
    };
    for (size_t i = 0; i < sizeof(colours) / sizeof(colours[0]); i++) {
        /* A colour the user wrote down wins over the theme's. Picking a
         * theme should never quietly undo a choice someone made. */
        ase_config_set_themed(config, colours[i].key, colours[i].value);
    }
    return true;
}
