#include "ase/config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

typedef struct {
    char *key;
    char *value;
} ConfigEntry;

struct AseConfig {
    ConfigEntry *entries;
    size_t count;
    size_t capacity;
};

/* Plain ISO C90 strdup — avoids relying on POSIX strdup/MSVC's _strdup. */
static char *ase_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len);
    return copy;
}

static void config_set(AseConfig *config, const char *key, const char *value) {
    for (size_t i = 0; i < config->count; i++) {
        if (strcmp(config->entries[i].key, key) == 0) {
            char *new_value = ase_strdup(value);
            if (new_value == NULL) {
                return;
            }
            free(config->entries[i].value);
            config->entries[i].value = new_value;
            return;
        }
    }

    if (config->count == config->capacity) {
        size_t new_cap = config->capacity == 0 ? 8 : config->capacity * 2;
        ConfigEntry *grown = (ConfigEntry *)realloc(config->entries, new_cap * sizeof(ConfigEntry));
        if (grown == NULL) {
            return;
        }
        config->entries = grown;
        config->capacity = new_cap;
    }

    char *key_copy = ase_strdup(key);
    char *value_copy = ase_strdup(value);
    if (key_copy == NULL || value_copy == NULL) {
        free(key_copy);
        free(value_copy);
        return;
    }
    config->entries[config->count].key = key_copy;
    config->entries[config->count].value = value_copy;
    config->count++;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';
    return s;
}

AseConfig *ase_config_create_default(void) {
    AseConfig *config = (AseConfig *)calloc(1, sizeof(AseConfig));
    if (config == NULL) {
        return NULL;
    }

    config_set(config, "background", "#282828");
    config_set(config, "text", "#F5E6C8");
    config_set(config, "selection", "#45403866");
    config_set(config, "find_match", "#45403899");
    config_set(config, "font_family", "monospace");
    config_set(config, "font_size", "12");

    return config;
}

static void config_parse_line(AseConfig *config, char *line) {
    char *trimmed = trim(line);
    if (*trimmed == '\0' || *trimmed == '#') {
        return;
    }

    char *eq = strchr(trimmed, '=');
    if (eq == NULL) {
        return;
    }

    *eq = '\0';
    char *key = trim(trimmed);
    char *value = trim(eq + 1);
    if (*key == '\0') {
        return;
    }

    config_set(config, key, value);
}

AseConfig *ase_config_load(const char *path) {
    AseConfig *config = ase_config_create_default();
    if (config == NULL || path == NULL) {
        return config;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return config; /* missing/unreadable file: defaults only */
    }

    char line[512];
    while (fgets(line, sizeof(line), f) != NULL) {
        config_parse_line(config, line);
    }
    fclose(f);

    return config;
}

void ase_config_destroy(AseConfig *config) {
    if (config == NULL) {
        return;
    }
    for (size_t i = 0; i < config->count; i++) {
        free(config->entries[i].key);
        free(config->entries[i].value);
    }
    free(config->entries);
    free(config);
}

const char *ase_config_get_string(const AseConfig *config, const char *key) {
    if (config == NULL || key == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < config->count; i++) {
        if (strcmp(config->entries[i].key, key) == 0) {
            return config->entries[i].value;
        }
    }
    return NULL;
}

long ase_config_get_int(const AseConfig *config, const char *key, long fallback) {
    const char *value = ase_config_get_string(config, key);
    if (value == NULL) {
        return fallback;
    }

    char *end;
    long parsed = strtol(value, &end, 10);
    if (end == value) {
        return fallback;
    }
    return parsed;
}

static bool parse_hex_byte(const char *s, uint8_t *out) {
    if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) {
        return false;
    }
    unsigned int value = 0;
    if (sscanf(s, "%2x", &value) != 1) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

bool ase_config_get_color(const AseConfig *config, const char *key,
                           uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    const char *value = ase_config_get_string(config, key);
    if (value == NULL || value[0] != '#') {
        return false;
    }

    size_t len = strlen(value);
    if (len != 7 && len != 9) { /* #RRGGBB or #RRGGBBAA */
        return false;
    }

    uint8_t rr, gg, bb, aa = 0xFF;
    if (!parse_hex_byte(value + 1, &rr)) {
        return false;
    }
    if (!parse_hex_byte(value + 3, &gg)) {
        return false;
    }
    if (!parse_hex_byte(value + 5, &bb)) {
        return false;
    }
    if (len == 9 && !parse_hex_byte(value + 7, &aa)) {
        return false;
    }

    *r = rr;
    *g = gg;
    *b = bb;
    *a = aa;
    return true;
}

char *ase_config_default_path(void) {
    const char *base;
    char fallback_base[4096];

#if defined(_WIN32)
    const char *suffix = "\\ase\\config.ase";
    base = getenv("APPDATA");
#else
    const char *suffix = "/ase/config.ase";
    base = getenv("XDG_CONFIG_HOME");
    if (base == NULL || base[0] == '\0') {
        const char *home = getenv("HOME");
        if (home == NULL) {
            return NULL;
        }
        if (snprintf(fallback_base, sizeof(fallback_base), "%s/.config", home) >= (int)sizeof(fallback_base)) {
            return NULL;
        }
        base = fallback_base;
    }
#endif

    if (base == NULL) {
        return NULL;
    }

    size_t len = strlen(base) + strlen(suffix) + 1;
    char *path = (char *)malloc(len);
    if (path == NULL) {
        return NULL;
    }
    snprintf(path, len, "%s%s", base, suffix);
    return path;
}

static const char kDefaultConfigTemplate[] =
    "# Absolute Simple Editor -- config & theme.\n"
    "#\n"
    "# Plain key = value pairs. Lines starting with # are comments.\n"
    "# This file is hot-reloaded: edit, save, and the running editor\n"
    "# picks up the change within a second -- no restart needed.\n"
    "\n"
    "# Colors are #RRGGBB or #RRGGBBAA hex.\n"
    "background = #282828\n"
    "text = #F5E6C8\n"
    "selection = #45403866\n"
    "find_match = #45403899\n"
    "\n"
    "font_family = monospace\n"
    "font_size = 12\n"
    "\n"
    "# Off by default (opt-in, per the aesthetic pillar and for anyone who\n"
    "# prefers reduced motion). Set true for a smooth caret fade instead of\n"
    "# a hard on/off blink.\n"
    "animations = false\n"
    "\n"
    "# off / absolute / relative (relative shows distance from the current\n"
    "# line, Vim-style, except the current line itself which stays absolute).\n"
    "line_numbers = absolute\n";

/* Creates only the immediate parent directory, not any missing
 * grandparent — see docs/adr/0008, decision 5. */
static void ensure_parent_dir_exists(const char *path) {
    const char *last_slash = strrchr(path, '/');
#if defined(_WIN32)
    const char *last_backslash = strrchr(path, '\\');
    if (last_backslash != NULL && (last_slash == NULL || last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
#endif
    if (last_slash == NULL) {
        return;
    }

    size_t dir_len = (size_t)(last_slash - path);
    char *dir = (char *)malloc(dir_len + 1);
    if (dir == NULL) {
        return;
    }
    memcpy(dir, path, dir_len);
    dir[dir_len] = '\0';

#if defined(_WIN32)
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif

    free(dir);
}

bool ase_config_write_default_if_missing(const char *path) {
    if (path == NULL) {
        return false;
    }

    FILE *existing = fopen(path, "r");
    if (existing != NULL) {
        fclose(existing);
        return true;
    }

    ensure_parent_dir_exists(path);

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return false;
    }
    bool ok = fputs(kDefaultConfigTemplate, f) != EOF;
    return fclose(f) == 0 && ok;
}
