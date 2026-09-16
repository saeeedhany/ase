#include "ase/config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>

#include "internal.h"
#endif

typedef struct {
    char *key;
    char *value;
} ConfigEntry;

struct AseConfig {
    ConfigEntry *entries;
    size_t count;
    size_t capacity;
    /* Scratch for ase_config_entries_with_prefix; owned here so the
     * caller never frees. */
    const char **scan_keys;
    const char **scan_values;
};

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
    {"syntax_max_kb", "1024", "Skip highlighting past this file size, in KB.", false},
    {"restore_session", "true", "Reopen last session's files when started bare.", false},
    {"git_marks", "true", "Gutter bars for lines changed since the last commit.", false},
    {"key.<chord>", NULL, "Run a command on a chord, e.g. key.ctrl+s = editor.save.", false},
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
    free(config->scan_keys);
    free(config->scan_values);
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
    /* The language name is the LSP `languageId`, so these are the
     * spec's spellings rather than the suffix — a server keys off
     * "python", never "py". A suffix missing here means no server can
     * start for it however it is configured, which is why the list is
     * broad; see docs/adr/0103. */
    {"c", "c"},           {"h", "c"},
    {"cpp", "cpp"},       {"cc", "cpp"},       {"cxx", "cpp"},
    {"hpp", "cpp"},       {"hh", "cpp"},       {"hxx", "cpp"},
    {"cs", "csharp"},     {"mm", "objective-cpp"},

    {"py", "python"},     {"pyi", "python"},
    {"rs", "rust"},       {"go", "go"},
    {"rb", "ruby"},       {"php", "php"},
    {"java", "java"},     {"kt", "kotlin"},    {"kts", "kotlin"},
    {"swift", "swift"},   {"scala", "scala"},
    {"hs", "haskell"},    {"ml", "ocaml"},     {"mli", "ocaml"},
    {"zig", "zig"},       {"dart", "dart"},    {"nim", "nim"},
    {"ex", "elixir"},     {"exs", "elixir"},
    {"erl", "erlang"},    {"hrl", "erlang"},
    {"jl", "julia"},      {"r", "r"},
    {"pl", "perl"},       {"pm", "perl"},
    {"lua", "lua"},       {"vim", "vim"},

    {"js", "javascript"}, {"mjs", "javascript"}, {"cjs", "javascript"},
    {"jsx", "javascriptreact"},
    {"ts", "typescript"}, {"mts", "typescript"}, {"cts", "typescript"},
    {"tsx", "typescriptreact"},
    {"html", "html"},     {"htm", "html"},
    {"css", "css"},       {"scss", "scss"},    {"less", "less"},

    {"sh", "shellscript"}, {"bash", "shellscript"}, {"zsh", "shellscript"},
    {"ps1", "powershell"},
    {"sql", "sql"},       {"proto", "proto"},  {"tf", "terraform"},
    {"json", "json"},     {"jsonc", "jsonc"},
    {"yaml", "yaml"},     {"yml", "yaml"},
    {"toml", "toml"},     {"xml", "xml"},      {"cmake", "cmake"},
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

/* The scratch arrays are owned by the config and rebuilt on each call,
 * so the caller never frees and a second call invalidates the first. */
bool ase_config_entries_with_prefix(const AseConfig *config, const char *prefix,
                                     const char *const **keys, const char *const **values,
                                     size_t *count) {
    if (count != NULL) {
        *count = 0;
    }
    if (config == NULL || prefix == NULL || keys == NULL || values == NULL || count == NULL) {
        return false;
    }
    size_t prefix_len = strlen(prefix);
    size_t matches = 0;
    for (size_t i = 0; i < config->count; i++) {
        if (strncmp(config->entries[i].key, prefix, prefix_len) == 0) {
            matches++;
        }
    }
    if (matches == 0) {
        return false;
    }

    free(config->scan_keys);
    free(config->scan_values);
    AseConfig *mutable_config = (AseConfig *)config;
    mutable_config->scan_keys = (const char **)malloc(matches * sizeof(char *));
    mutable_config->scan_values = (const char **)malloc(matches * sizeof(char *));
    if (mutable_config->scan_keys == NULL || mutable_config->scan_values == NULL) {
        free(mutable_config->scan_keys);
        free(mutable_config->scan_values);
        mutable_config->scan_keys = NULL;
        mutable_config->scan_values = NULL;
        return false;
    }

    size_t at = 0;
    for (size_t i = 0; i < config->count; i++) {
        if (strncmp(config->entries[i].key, prefix, prefix_len) == 0) {
            mutable_config->scan_keys[at] = config->entries[i].key;
            mutable_config->scan_values[at] = config->entries[i].value;
            at++;
        }
    }
    *keys = mutable_config->scan_keys;
    *values = mutable_config->scan_values;
    *count = matches;
    return true;
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
    "# Syntax highlighting parses the whole file, however little of it is\n"
    "# on screen, and that cost grows with it: 25ms at 113KB of C, 230ms\n"
    "# at 1MB, 1.07s at 4.5MB. Past this size a file opens with no colours\n"
    "# instead of freezing first, and the status bar says so. Raise it if\n"
    "# you would rather wait, or set 0 to never skip. See docs/adr/0107.\n"
    "syntax_max_kb = 1024\n"
    "\n"
    "# Keys. `key.<chord> = <command>` binds a chord to a command; press\n"
    "# F1 for every command name. A chord is modifiers and a key, in any\n"
    "# order and any case: ctrl+s, Ctrl+Shift+F, alt+left, f5. Prefix it\n"
    "# with a Vim mode to bind only there: key.normal.ctrl+d. Use `none`\n"
    "# to switch a default off. Bare keys cannot be bound -- a chord needs\n"
    "# Ctrl, Alt or Meta, or to be a function key -- so no binding can\n"
    "# make the editor untypeable. See docs/adr/0113.\n"
    "# key.ctrl+s = editor.save\n"
    "# key.f5 = editor.compile\n"
    "# key.ctrl+b = none\n"
    "\n"
    "# A thin bar in the gutter for each line added, changed or removed\n"
    "# since the last commit, read from `git diff`. Needs git on PATH; a\n"
    "# file outside a repository simply has no marks. They describe the\n"
    "# file on disk, so they hide while an unsaved edit has moved the\n"
    "# lines around, and come back on save. See docs/adr/0112.\n"
    "git_marks = true\n"
    "\n"
    "# Started with no file argument, reopen whatever was open when you\n"
    "# last quit, with the caret and scroll position where you left them.\n"
    "# `ase file.c` always means that file, never the session. Unsaved\n"
    "# changes are a separate mechanism and always recovered. ADR 0111.\n"
    "restore_session = true\n"
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
    "# What a suffix means. Most languages are built in, named with the\n"
    "# LSP spec's own id, so `lang.python.lsp = pylsp` is all a Python\n"
    "# file needs. Override per suffix for anything else — .h is the one\n"
    "# worth knowing about, since a C++ project using .h headers wants\n"
    "# it. The name is both the highlighting language and the LSP\n"
    "# languageId, so a language with no grammar still gets a server.\n"
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
