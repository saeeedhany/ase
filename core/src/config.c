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

static const AseConfigKeyDoc kKeyDocs[] = {
    {"background", "#282828", "Page colour. #RRGGBB or #RRGGBBAA.", false},
    {"text", "#F5E6C8", "Text colour.", false},
    {"selection", "#45403866", "Selection highlight.", false},
    {"find_match", "#45403899", "The current find/replace match.", false},
    {"panel_background", "#282828E6", "Floating panels (find, open, help).", false},
    {"diagnostic_error", "#E06C75", "Language-server error underline.", false},
    {"diagnostic_warning", "#E5C07B", "Language-server warning underline.", false},
    {"syntax_type", "#689d6a", "Types. One of only two syntax colours.", false},
    {"syntax_string", "#d79921", "String literals. The other one.", false},
    {"font_family", "monospace", "Editor font.", false},
    {"font_size", "11", "Point size. Ctrl+= / Ctrl+- override it live.", false},
    {"vim_mode", "true", "Modal editing. false for always-insert.", false},
    {"animations", "false", "true for a smooth caret fade instead of a blink.", false},
    {"line_numbers", "absolute", "off, absolute, or relative.", false},
    {"max_fps", NULL, "Cap the animation rate; unset follows the display.", false},
    {"build_command", NULL, ":compile runs this; %f is the current file.", false},
    {"lsp_command", NULL, "Language server for any language without its own.", false},
    {"lang.<id>.lsp", NULL, "Language server for one language, e.g. lang.cpp.lsp.", false},
    {"filetype.<suffix>", NULL, "What a suffix means, e.g. filetype.h = cpp.", true},
};

const AseConfigKeyDoc *ase_config_key_docs(size_t *count) {
    if (count != NULL) {
        *count = sizeof(kKeyDocs) / sizeof(kKeyDocs[0]);
    }
    return kKeyDocs;
}

AseConfig *ase_config_create_default(void) {
    AseConfig *config = (AseConfig *)calloc(1, sizeof(AseConfig));
    if (config == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(kKeyDocs) / sizeof(kKeyDocs[0]); i++) {
        /* A family placeholder has no key to set, and an unset key has
         * no value to set it to. */
        if (kKeyDocs[i].value != NULL && strchr(kKeyDocs[i].key, '<') == NULL) {
            config_set(config, kKeyDocs[i].key, kKeyDocs[i].value);
        }
    }

    return config;
}

bool ase_config_key_allowed_in_project(const char *key) {
    if (key == NULL) {
        return false;
    }
    return strncmp(key, "filetype.", sizeof("filetype.") - 1) == 0;
}

static void config_parse_line(AseConfig *config, char *line, bool project, size_t *refused) {
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
    if (project && !ase_config_key_allowed_in_project(key)) {
        if (refused != NULL) {
            (*refused)++;
        }
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
        config_parse_line(config, line, false, NULL);
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

/* A dot in a parent directory is not a suffix, hence the separator
 * scan first. */
static const char *path_suffix(const char *path) {
    const char *name = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot == name || dot[1] == '\0') {
        return NULL;
    }
    return dot + 1;
}

static const struct {
    const char *suffix;
    const char *language;
} kBuiltinFiletypes[] = {
    {"c", "c"},     {"h", "c"},     {"cpp", "cpp"}, {"cc", "cpp"},
    {"cxx", "cpp"}, {"hpp", "cpp"}, {"hh", "cpp"},  {"hxx", "cpp"},
};

/* Lowercased into `out` so FOO.C resolves; anything longer than the
 * buffer is not a suffix we know. */
static bool suffix_to_lower(const char *suffix, char *out, size_t out_size) {
    size_t len = strlen(suffix);
    if (len == 0 || len >= out_size) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = (char)tolower((unsigned char)suffix[i]);
    }
    out[len] = '\0';
    return true;
}

static bool is_sep(char c) {
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

char *ase_config_find_project_file(const char *start_path) {
    if (start_path == NULL) {
        return NULL;
    }

    size_t path_len = strlen(start_path);
    size_t name_len = strlen(ASE_PROJECT_CONFIG_NAME);
    char *buf = (char *)malloc(path_len + name_len + 2);
    if (buf == NULL) {
        return NULL;
    }

    /* `len` is how much of start_path forms the directory being tried,
     * trailing separator included. */
    size_t len = path_len;
    while (len > 0 && !is_sep(start_path[len - 1])) {
        len--;
    }

    /* A relative path runs out at len == 0 rather than reaching a root;
     * the depth cap is only a guard against a pathological path. */
    for (int depth = 0; depth < 64 && len > 0; depth++) {
        memcpy(buf, start_path, len);
        memcpy(buf + len, ASE_PROJECT_CONFIG_NAME, name_len + 1);
        if (file_exists(buf)) {
            return buf;
        }
        len--;
        while (len > 0 && !is_sep(start_path[len - 1])) {
            len--;
        }
    }

    free(buf);
    return NULL;
}

bool ase_config_overlay_project(AseConfig *config, const char *path, size_t *refused_out) {
    if (refused_out != NULL) {
        *refused_out = 0;
    }
    if (config == NULL || path == NULL) {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }

    size_t refused = 0;
    char line[512];
    while (fgets(line, sizeof(line), f) != NULL) {
        config_parse_line(config, line, true, &refused);
    }
    fclose(f);

    if (refused_out != NULL) {
        *refused_out = refused;
    }
    return true;
}

const char *ase_config_language_for_path(const AseConfig *config, const char *path) {
    if (path == NULL) {
        return NULL;
    }
    const char *suffix = path_suffix(path);
    if (suffix == NULL) {
        return NULL;
    }

    char lower[32];
    if (!suffix_to_lower(suffix, lower, sizeof(lower))) {
        return NULL;
    }

    char key[64];
    if (snprintf(key, sizeof(key), "filetype.%s", lower) < (int)sizeof(key)) {
        const char *override = ase_config_get_string(config, key);
        if (override != NULL && *override != '\0') {
            return override;
        }
    }

    for (size_t i = 0; i < sizeof(kBuiltinFiletypes) / sizeof(kBuiltinFiletypes[0]); i++) {
        if (strcmp(kBuiltinFiletypes[i].suffix, lower) == 0) {
            return kBuiltinFiletypes[i].language;
        }
    }
    return NULL;
}

const char *ase_config_get_lang_string(const AseConfig *config, const char *language,
                                        const char *key) {
    if (language == NULL || key == NULL) {
        return NULL;
    }
    char full[128];
    if (snprintf(full, sizeof(full), "lang.%s.%s", language, key) >= (int)sizeof(full)) {
        return NULL;
    }
    return ase_config_get_string(config, full);
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
    "# Floating chrome (find/replace and future panels) — background-\n"
    "# tinted with a small transparency; the text/badges inside always\n"
    "# render at full contrast, unaffected by this alpha.\n"
    "panel_background = #282828E6\n"
    "\n"
    "# LSP diagnostic severity — the squiggly underline and gutter dot.\n"
    "diagnostic_error = #E06C75\n"
    "diagnostic_warning = #E5C07B\n"
    "\n"
    "# Syntax highlighting is otherwise monochrome (one font color, only\n"
    "# weight/opacity vary) -- these two are the deliberate exception,\n"
    "# used for types and string literals only. See docs/adr/0048.\n"
    "syntax_type = #689d6a\n"
    "syntax_string = #d79921\n"
    "\n"
    "font_family = monospace\n"
    "font_size = 11\n"
    "\n"
    "# Off by default (opt-in, per the aesthetic pillar and for anyone who\n"
    "# prefers reduced motion). Set true for a smooth caret fade instead of\n"
    "# a hard on/off blink.\n"
    "animations = false\n"
    "\n"
    "# off / absolute / relative (relative shows distance from the current\n"
    "# line, Vim-style, except the current line itself which stays absolute).\n"
    "line_numbers = absolute\n"
    "\n"
    "# Animation runs at the display's refresh rate -- a 144Hz screen\n"
    "# animates at 144Hz. Set this to cap it (battery), not to raise it:\n"
    "# the display's own rate is always the ceiling.\n"
    "# max_fps = 60\n"
    "\n"
    "# On by default. Modal (Vim-style) editing: Normal/Insert/Visual\n"
    "# modes, motions, operators, counts. Set false for plain, always-\n"
    "# insert editing instead. See docs/adr/0046, docs/adr/0050.\n"
    "vim_mode = true\n"
    "\n"
    "# :compile's shell command — %f is replaced with the current file's\n"
    "# path, run with the file's directory as cwd. No default: an\n"
    "# unconfigured build_command is reported as such, not guessed.\n"
    "# build_command = gcc %f -o /tmp/a.out && /tmp/a.out\n"
    "\n"
    "# Language server to spawn, per language — diagnostics only for now\n"
    "# (see docs/adr/0029). No default, same reasoning as build_command:\n"
    "# an unconfigured server just means no LSP features, not a guess at\n"
    "# what you have installed. `lsp_command` still applies to every\n"
    "# language that has no entry of its own.\n"
    "# lang.c.lsp = clangd\n"
    "# lang.cpp.lsp = clangd\n"
    "\n"
    "# What a suffix means. Built in: .c/.h are c, .cpp/.cc/.cxx/.hpp/.hh/\n"
    "# .hxx are cpp. Override per suffix — .h is the one worth knowing\n"
    "# about, since a C++ project that uses .h headers wants this. The\n"
    "# name is both the highlighting language and the LSP languageId, so\n"
    "# a language with no grammar can still have a server.\n"
    "# filetype.h = cpp\n"
    "\n"
    "# Per-project settings go in a .ase.conf beside your code (the\n"
    "# nearest one at or above the file wins). It may only set what files\n"
    "# mean -- filetype.* -- and never what commands to run, because a\n"
    "# repository you cloned writes it. Anything else in it is ignored,\n"
    "# and the editor says so. See docs/adr/0087.\n";

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
