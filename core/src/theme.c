#include "ase/theme.h"

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

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

#define BUILTIN_COUNT (sizeof(kThemes) / sizeof(kThemes[0]))

/* ---- themes loaded from files (ADR 0133) ---- */

/* Owned copies, unlike the built-ins, which are string literals. Kept
 * beside the theme so one free() per entry releases all of it. */
typedef struct {
    AseTheme theme;
    char *storage[11];
} LoadedTheme;

static LoadedTheme *g_loaded = NULL;
static size_t g_loaded_count = 0;

static void loaded_free(LoadedTheme *entry) {
    for (size_t i = 0; i < sizeof(entry->storage) / sizeof(entry->storage[0]); i++) {
        free(entry->storage[i]);
        entry->storage[i] = NULL;
    }
}

void ase_theme_unload(void) {
    for (size_t i = 0; i < g_loaded_count; i++) {
        loaded_free(&g_loaded[i]);
    }
    free(g_loaded);
    g_loaded = NULL;
    g_loaded_count = 0;
}

static const AseTheme *builtin_named(const char *name) {
    for (size_t i = 0; i < BUILTIN_COUNT; i++) {
        if (strcmp(kThemes[i].name, name) == 0) {
            return &kThemes[i];
        }
    }
    return NULL;
}

/* Every field, in the order `storage` holds them, so one loop fills
 * them and one loop frees them. The first two are the name and the
 * summary; the rest are the colours, and their keys are the config
 * keys of the same name. */
static const char *kColourKeys[] = {
    "background",   "text",          "selection",        "find_match",
    "panel_background", "syntax_type", "syntax_string",  "diagnostic_error",
    "diagnostic_warning",
};

static bool load_one(const char *path, const char *name, LoadedTheme *out) {
    AseConfig *config = ase_config_load(path);
    if (config == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    out->storage[0] = ase_strdup(name);
    const char *summary = ase_config_get_string(config, "summary");
    /* Not a config key, so it is never a default; absent means absent. */
    out->storage[1] =
        ase_strdup((summary != NULL && *summary != '\0') ? summary : "from a file");

    bool ok = out->storage[0] != NULL && out->storage[1] != NULL;
    for (size_t i = 0; ok && i < sizeof(kColourKeys) / sizeof(kColourKeys[0]); i++) {
        /* ase_config_load() overlays the file onto the shipped
         * defaults, so a key the file omits already reads as the
         * default — which is exactly the inheritance wanted. */
        const char *value = ase_config_get_string(config, kColourKeys[i]);
        out->storage[2 + i] = ase_strdup(value != NULL ? value : "");
        ok = out->storage[2 + i] != NULL;
    }
    ase_config_destroy(config);
    if (!ok) {
        loaded_free(out);
        return false;
    }

    out->theme.name = out->storage[0];
    out->theme.summary = out->storage[1];
    out->theme.background = out->storage[2];
    out->theme.text = out->storage[3];
    out->theme.selection = out->storage[4];
    out->theme.find_match = out->storage[5];
    out->theme.panel_background = out->storage[6];
    out->theme.syntax_type = out->storage[7];
    out->theme.syntax_string = out->storage[8];
    out->theme.diagnostic_error = out->storage[9];
    out->theme.diagnostic_warning = out->storage[10];
    return true;
}

static const char kSuffix[] = ".ase";
#define SUFFIX_LEN (sizeof(kSuffix) - 1)

/* The file's name without its suffix, or false when it has none. */
static bool theme_name_of(const char *file_name, char *out, size_t out_size) {
    size_t len = strlen(file_name);
    if (len <= SUFFIX_LEN || strcmp(file_name + len - SUFFIX_LEN, kSuffix) != 0) {
        return false;
    }
    size_t name_len = len - SUFFIX_LEN;
    if (name_len + 1 > out_size) {
        return false;
    }
    memcpy(out, file_name, name_len);
    out[name_len] = '\0';
    return true;
}

static void append_loaded(const char *path, const char *name) {
    LoadedTheme entry;
    if (!load_one(path, name, &entry)) {
        return;
    }
    /* A second file claiming a name the first one took: the first wins,
     * so the set never holds two of the same name. */
    for (size_t i = 0; i < g_loaded_count; i++) {
        if (strcmp(g_loaded[i].theme.name, name) == 0) {
            loaded_free(&entry);
            return;
        }
    }
    LoadedTheme *grown = (LoadedTheme *)realloc(g_loaded, (g_loaded_count + 1) * sizeof(*grown));
    if (grown == NULL) {
        loaded_free(&entry);
        return;
    }
    g_loaded = grown;
    g_loaded[g_loaded_count++] = entry;
}

size_t ase_theme_load_directory(const char *dir) {
    ase_theme_unload();
    if (dir == NULL) {
        return 0;
    }

#if defined(_WIN32)
    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s\\*%s", dir, kSuffix) >= (int)sizeof(pattern)) {
        return 0;
    }
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return 0; /* nothing there is not a failure */
    }
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        char name[256];
        char full[MAX_PATH];
        if (!theme_name_of(data.cFileName, name, sizeof(name)) ||
            snprintf(full, sizeof(full), "%s\\%s", dir, data.cFileName) >= (int)sizeof(full)) {
            continue;
        }
        append_loaded(full, name);
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        return 0; /* nothing there is not a failure */
    }
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char name[256];
        char full[4096];
        if (!theme_name_of(entry->d_name, name, sizeof(name)) ||
            snprintf(full, sizeof(full), "%s/%s", dir, entry->d_name) >= (int)sizeof(full)) {
            continue;
        }
        append_loaded(full, name);
    }
    closedir(dp);
#endif
    return g_loaded_count;
}

/*
 * A loaded theme named like a built-in replaces it rather than sitting
 * beside it, so the count does not grow and `:theme` does not list the
 * name twice.
 */
static bool shadows_a_builtin(size_t loaded_index) {
    return builtin_named(g_loaded[loaded_index].theme.name) != NULL;
}

size_t ase_theme_count(void) {
    size_t shadowing = 0;
    for (size_t i = 0; i < g_loaded_count; i++) {
        if (shadows_a_builtin(i)) {
            shadowing++;
        }
    }
    return BUILTIN_COUNT + g_loaded_count - shadowing;
}

const AseTheme *ase_theme_at(size_t index) {
    /* Built-ins first, in their shipped order, each replaced in place
     * by a file of the same name; then whatever is left. */
    if (index < BUILTIN_COUNT) {
        const AseTheme *shadow = NULL;
        for (size_t i = 0; i < g_loaded_count; i++) {
            if (strcmp(g_loaded[i].theme.name, kThemes[index].name) == 0) {
                shadow = &g_loaded[i].theme;
                break;
            }
        }
        return shadow != NULL ? shadow : &kThemes[index];
    }
    size_t wanted = index - BUILTIN_COUNT;
    for (size_t i = 0; i < g_loaded_count; i++) {
        if (shadows_a_builtin(i)) {
            continue;
        }
        if (wanted-- == 0) {
            return &g_loaded[i].theme;
        }
    }
    return NULL;
}

const AseTheme *ase_theme_find(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    /* Files first: one named after a built-in was put there to replace
     * it. */
    for (size_t i = 0; i < g_loaded_count; i++) {
        if (strcmp(g_loaded[i].theme.name, name) == 0) {
            return &g_loaded[i].theme;
        }
    }
    return builtin_named(name);
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
