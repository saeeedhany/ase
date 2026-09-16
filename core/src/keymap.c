#include "ase/keymap.h"

#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Keys this editor can name in a binding. Single letters and digits are
 * themselves; everything else needs a word, because the config file is
 * plain text and `ctrl+=` has to survive being read back by eye.
 *
 * `alias` is what a user might reasonably type instead. One is enough:
 * offering three spellings of Escape is how a table stops being
 * readable, and the canonical name is always accepted.
 */
static const struct {
    const char *name;
    const char *alias;
} kNamedKeys[] = {
    {"escape", "esc"},      {"tab", NULL},         {"return", "enter"},
    {"backspace", NULL},    {"delete", "del"},     {"space", NULL},
    {"left", NULL},         {"right", NULL},       {"up", NULL},
    {"down", NULL},         {"home", NULL},        {"end", NULL},
    {"pageup", NULL},       {"pagedown", NULL},    {"insert", NULL},
    {"semicolon", ";"},     {"equal", "="},        {"plus", "+"},
    {"minus", "-"},         {"comma", ","},        {"period", "."},
    {"slash", "/"},         {"backslash", "\\"},   {"apostrophe", "'"},
    {"bracketleft", "["},   {"bracketright", "]"}, {"grave", "`"},
};

/* The canonical name for `text`, or NULL when it names no key. */
static const char *resolve_key(const char *text) {
    if (text == NULL || text[0] == '\0') {
        return NULL;
    }
    /* A single letter or digit is its own name. */
    if (text[1] == '\0' && isalnum((unsigned char)text[0])) {
        return text;
    }
    /* f1 through f12, and no further: f13 exists on almost no keyboard
     * and accepting it would accept f99 too. */
    if ((text[0] == 'f') && isdigit((unsigned char)text[1])) {
        const char *digits = text + 1;
        if (strlen(digits) > 2) {
            return NULL;
        }
        for (const char *p = digits; *p != '\0'; p++) {
            if (!isdigit((unsigned char)*p)) {
                return NULL;
            }
        }
        int n = atoi(digits);
        return (n >= 1 && n <= 12) ? text : NULL;
    }
    for (size_t i = 0; i < sizeof(kNamedKeys) / sizeof(kNamedKeys[0]); i++) {
        if (strcmp(text, kNamedKeys[i].name) == 0) {
            return kNamedKeys[i].name;
        }
        if (kNamedKeys[i].alias != NULL && strcmp(text, kNamedKeys[i].alias) == 0) {
            return kNamedKeys[i].name;
        }
    }
    return NULL;
}

bool ase_keymap_is_known_key(const char *name) {
    if (name == NULL) {
        return false;
    }
    char lowered[32];
    size_t len = strlen(name);
    if (len == 0 || len >= sizeof(lowered)) {
        return false;
    }
    for (size_t i = 0; i <= len; i++) {
        lowered[i] = (char)tolower((unsigned char)name[i]);
    }
    return resolve_key(lowered) != NULL;
}

bool ase_keymap_format(bool ctrl, bool alt, bool shift, bool meta, const char *key, char *out,
                       size_t out_size) {
    if (key == NULL || out == NULL || out_size == 0) {
        return false;
    }
    char lowered[32];
    size_t len = strlen(key);
    if (len == 0 || len >= sizeof(lowered)) {
        return false;
    }
    for (size_t i = 0; i <= len; i++) {
        lowered[i] = (char)tolower((unsigned char)key[i]);
    }
    const char *resolved = resolve_key(lowered);
    if (resolved == NULL) {
        return false;
    }

    /* One order, always, so two spellings of a chord are one string. */
    char built[96];
    int written = snprintf(built, sizeof(built), "%s%s%s%s%s", ctrl ? "ctrl+" : "",
                           alt ? "alt+" : "", shift ? "shift+" : "", meta ? "meta+" : "",
                           resolved);
    if (written < 0 || (size_t)written >= sizeof(built) || (size_t)written >= out_size) {
        return false;
    }
    memcpy(out, built, (size_t)written + 1);
    return true;
}

bool ase_keymap_canonical(const char *text, char *out, size_t out_size) {
    if (text == NULL || out == NULL || out_size == 0) {
        return false;
    }

    bool ctrl = false, alt = false, shift = false, meta = false;
    char key[32] = {0};
    bool have_key = false;

    const char *cursor = text;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        /* A '+' here is the key itself rather than a separator: the end
         * of the string, or another '+' following it. */
        const char *plus = strchr(cursor, '+');
        const char *token_end;
        if (plus == cursor) {
            token_end = cursor + 1; /* the literal '+' key */
        } else if (plus != NULL) {
            token_end = plus;
        } else {
            token_end = cursor + strlen(cursor);
        }

        size_t token_len = (size_t)(token_end - cursor);
        while (token_len > 0 && (cursor[token_len - 1] == ' ' || cursor[token_len - 1] == '\t')) {
            token_len--;
        }
        if (token_len == 0 || token_len >= sizeof(key)) {
            return false;
        }
        char token[32];
        for (size_t i = 0; i < token_len; i++) {
            token[i] = (char)tolower((unsigned char)cursor[i]);
        }
        token[token_len] = '\0';

        bool is_modifier = true;
        if (strcmp(token, "ctrl") == 0 || strcmp(token, "control") == 0) {
            if (ctrl) {
                return false; /* named twice */
            }
            ctrl = true;
        } else if (strcmp(token, "alt") == 0) {
            if (alt) {
                return false;
            }
            alt = true;
        } else if (strcmp(token, "shift") == 0) {
            if (shift) {
                return false;
            }
            shift = true;
        } else if (strcmp(token, "meta") == 0 || strcmp(token, "super") == 0 ||
                   strcmp(token, "cmd") == 0) {
            if (meta) {
                return false;
            }
            meta = true;
        } else {
            is_modifier = false;
        }

        if (!is_modifier) {
            if (have_key) {
                return false; /* two keys in one chord */
            }
            memcpy(key, token, token_len + 1);
            have_key = true;
        }

        cursor = token_end;
        if (*cursor == '+') {
            cursor++;
            /* A trailing '+' with nothing after it is not a chord. */
            if (*cursor == '\0') {
                return false;
            }
        }
    }

    if (!have_key) {
        return false;
    }
    return ase_keymap_format(ctrl, alt, shift, meta, key, out, out_size);
}
