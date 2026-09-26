#include "ase/build.h"

#include "internal.h"

#include "ase/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#define ASE_ACCESS(p) (_access((p), 0) == 0)
#else
#include <unistd.h>
#define ASE_ACCESS(p) (access((p), F_OK) == 0)
#endif

/*
 * Ordered most specific first, so a configured build directory beats
 * the file that would configure one. Every command is what the tool's
 * own documentation tells you to type — none of them is clever.
 */
static const struct {
    const char *marker;
    const char *command;
} kMarkers[] = {
    {"build/CMakeCache.txt", "cmake --build build"},
    {"build.ninja", "ninja"},
    {"CMakeLists.txt", "cmake -B build && cmake --build build"},
    {"Makefile", "make"},
    {"makefile", "make"},
    {"GNUmakefile", "make"},
    {"Cargo.toml", "cargo build"},
    {"go.mod", "go build ./..."},
    {"meson.build", "meson compile -C build"},
    {"package.json", "npm run build"},
};

static const size_t kMarkerCount = sizeof(kMarkers) / sizeof(kMarkers[0]);

size_t ase_build_marker_count(void) {
    return kMarkerCount;
}

const char *ase_build_marker_name(size_t index) {
    return index < kMarkerCount ? kMarkers[index].marker : NULL;
}

const char *ase_build_marker_command(size_t index) {
    return index < kMarkerCount ? kMarkers[index].command : NULL;
}

static bool is_separator(char c) {
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/* The directory holding `path`, or NULL when there is none. */
static char *parent_of(const char *path) {
    size_t len = strlen(path);
    while (len > 0 && is_separator(path[len - 1])) {
        len--; /* a trailing separator is not a component */
    }
    while (len > 0 && !is_separator(path[len - 1])) {
        len--;
    }
    while (len > 1 && is_separator(path[len - 1])) {
        len--;
    }
    if (len == 0) {
        return NULL;
    }
    char *copy = ase_memdup(path, len + 1);
    if (copy == NULL) {
        return NULL;
    }
    copy[len] = '\0';
    return copy;
}

static char *join(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    bool needs_sep = dir_len > 0 && !is_separator(dir[dir_len - 1]);
    size_t size = dir_len + (needs_sep ? 1 : 0) + strlen(name) + 1;
    char *out = (char *)malloc(size);
    if (out == NULL) {
        return NULL;
    }
    snprintf(out, size, "%s%s%s", dir, needs_sep ? "/" : "", name);
    return out;
}

static AseBuildGuess *guess_create(const char *command, const char *directory,
                                    const char *evidence, bool single_file) {
    AseBuildGuess *guess = (AseBuildGuess *)calloc(1, sizeof(AseBuildGuess));
    if (guess == NULL) {
        return NULL;
    }
    guess->command = ase_strdup(command);
    guess->directory = ase_strdup(directory);
    guess->evidence = ase_strdup(evidence);
    guess->single_file = single_file;
    if (guess->command == NULL || guess->directory == NULL || guess->evidence == NULL) {
        ase_build_guess_destroy(guess);
        return NULL;
    }
    return guess;
}

void ase_build_guess_destroy(AseBuildGuess *guess) {
    if (guess == NULL) {
        return;
    }
    free(guess->command);
    free(guess->directory);
    free(guess->evidence);
    free(guess);
}

static char *read_whole_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *text = (char *)malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(f);
        return NULL;
    }
    size_t read = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[read] = '\0';
    *out_len = read;
    return text;
}

/*
 * The build system's own answer for this one file. An entry carries
 * either `command` (a string) or `arguments` (an array); both shapes are
 * in the spec and cmake and bear emit different ones.
 */
static AseBuildGuess *guess_from_compile_commands(const char *db_path, const char *file_path) {
    size_t len = 0;
    char *text = read_whole_file(db_path, &len);
    if (text == NULL) {
        return NULL;
    }
    AseJsonValue *root = ase_json_parse(text, len);
    free(text);
    if (root == NULL || ase_json_type(root) != ASE_JSON_ARRAY) {
        ase_json_destroy(root);
        return NULL;
    }

    AseBuildGuess *guess = NULL;
    for (size_t i = 0; i < ase_json_array_size(root) && guess == NULL; i++) {
        AseJsonValue *entry = ase_json_array_get(root, i);
        const char *entry_file = ase_json_get_string(ase_json_object_get(entry, "file"));
        const char *directory = ase_json_get_string(ase_json_object_get(entry, "directory"));
        if (entry_file == NULL || directory == NULL || strcmp(entry_file, file_path) != 0) {
            continue;
        }

        const char *command = ase_json_get_string(ase_json_object_get(entry, "command"));
        if (command != NULL) {
            guess = guess_create(command, directory, db_path, true);
            break;
        }

        /* `arguments` instead: joined back into a command line. Nothing
         * here quotes, because nothing here runs it — it is shown to a
         * person first, who can see a path with a space in it. */
        AseJsonValue *arguments = ase_json_object_get(entry, "arguments");
        if (arguments == NULL || ase_json_type(arguments) != ASE_JSON_ARRAY) {
            continue;
        }
        size_t total = 1;
        for (size_t a = 0; a < ase_json_array_size(arguments); a++) {
            const char *arg = ase_json_get_string(ase_json_array_get(arguments, a));
            total += (arg != NULL ? strlen(arg) : 0) + 1;
        }
        char *joined = (char *)malloc(total);
        if (joined == NULL) {
            break;
        }
        joined[0] = '\0';
        size_t used = 0;
        for (size_t a = 0; a < ase_json_array_size(arguments); a++) {
            const char *arg = ase_json_get_string(ase_json_array_get(arguments, a));
            if (arg == NULL) {
                continue;
            }
            int written = snprintf(joined + used, total - used, "%s%s", used > 0 ? " " : "", arg);
            if (written > 0) {
                used += (size_t)written;
            }
        }
        guess = guess_create(joined, directory, db_path, true);
        free(joined);
    }

    ase_json_destroy(root);
    return guess;
}

/*
 * Walks up collecting candidates rather than stopping at the first.
 *
 * The nearest marker is usually the wrong one: a CMakeLists.txt in a
 * subdirectory describes a component, and the project it belongs to is
 * further up. So the outermost candidate wins — bounded by the
 * repository, because without a bound the walk reaches a home directory
 * with somebody else's Makefile in it.
 *
 * A compile_commands.json entry is the exception, and takes the nearest:
 * it is an exact answer about this file rather than a guess about the
 * tree around it.
 */
AseBuildGuess *ase_build_infer(const char *file_path, const char *stop_at) {
    if (file_path == NULL) {
        return NULL;
    }

    char *dir = parent_of(file_path);
    AseBuildGuess *outermost = NULL;

    while (dir != NULL) {
        char *db = join(dir, "compile_commands.json");
        if (db != NULL) {
            AseBuildGuess *exact = NULL;
            if (ASE_ACCESS(db)) {
                exact = guess_from_compile_commands(db, file_path);
            }
            free(db);
            if (exact != NULL) {
                ase_build_guess_destroy(outermost);
                free(dir);
                return exact;
            }
        }

        for (size_t i = 0; i < kMarkerCount; i++) {
            char *candidate = join(dir, kMarkers[i].marker);
            if (candidate == NULL) {
                continue;
            }
            if (ASE_ACCESS(candidate)) {
                /* Replaces whatever a directory below found: this one is
                 * further out, so it is the more likely project. */
                ase_build_guess_destroy(outermost);
                outermost = guess_create(kMarkers[i].command, dir, candidate, false);
                free(candidate);
                break;
            }
            free(candidate);
        }

        bool at_bound = (stop_at != NULL && strcmp(dir, stop_at) == 0);
        if (!at_bound) {
            char *git = join(dir, ".git");
            if (git != NULL) {
                at_bound = ASE_ACCESS(git);
                free(git);
            }
        }
        if (at_bound) {
            break;
        }

        char *parent = parent_of(dir);
        free(dir);
        dir = parent;
    }

    free(dir);
    return outermost;
}

/* ------------------------------------------------------- output parsing */

struct AseBuildDiagnostics {
    AseBuildDiagnostic *items;
    size_t count;
    size_t capacity;
};

size_t ase_build_diagnostic_count(const AseBuildDiagnostics *diagnostics) {
    return diagnostics == NULL ? 0 : diagnostics->count;
}

const AseBuildDiagnostic *ase_build_diagnostic_at(const AseBuildDiagnostics *diagnostics,
                                                    size_t index) {
    if (diagnostics == NULL || index >= diagnostics->count) {
        return NULL;
    }
    return &diagnostics->items[index];
}

void ase_build_diagnostics_destroy(AseBuildDiagnostics *diagnostics) {
    if (diagnostics == NULL) {
        return;
    }
    for (size_t i = 0; i < diagnostics->count; i++) {
        free(diagnostics->items[i].file);
        free(diagnostics->items[i].message);
    }
    free(diagnostics->items);
    free(diagnostics);
}

static bool diagnostics_push(AseBuildDiagnostics *into, const char *file, size_t file_len, int line,
                              int column, int severity, const char *message) {
    if (into->count == into->capacity) {
        size_t new_cap = into->capacity == 0 ? 8 : into->capacity * 2;
        AseBuildDiagnostic *grown =
            (AseBuildDiagnostic *)realloc(into->items, new_cap * sizeof(AseBuildDiagnostic));
        if (grown == NULL) {
            return false;
        }
        into->items = grown;
        into->capacity = new_cap;
    }
    AseBuildDiagnostic *item = &into->items[into->count];
    item->file = ase_memdup(file, file_len + 1);
    if (item->file == NULL) {
        return false;
    }
    item->file[file_len] = '\0';
    item->message = ase_strdup(message);
    if (item->message == NULL) {
        free(item->file);
        return false;
    }
    item->line = line;
    item->column = column;
    item->severity = severity;
    into->count++;
    return true;
}

/* "error", "warning" or "note", or 0 for anything else. The words are
 * the same in gcc, clang and MSVC; only the punctuation around them
 * differs. */
static int severity_of(const char *word, size_t len) {
    if (len == 5 && strncmp(word, "error", 5) == 0) {
        return 1;
    }
    if (len == 7 && strncmp(word, "warning", 7) == 0) {
        return 2;
    }
    if (len == 4 && strncmp(word, "note", 4) == 0) {
        return 3;
    }
    /* clang and gcc both say "fatal error". The word after it is
     * skipped along with MSVC's error code, by the same step. */
    if (len == 5 && strncmp(word, "fatal", 5) == 0) {
        return 1;
    }
    return 0;
}

static const char *skip_spaces(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }
    return p;
}

/*
 * One line of output, in either of the two shapes compilers print:
 *
 *   path/file.c:12:5: error: message        gcc, clang
 *   path\file.c(12,5): error C2065: message  MSVC
 *
 * The column is optional in both. Anything that does not match is not a
 * diagnostic, which is most of a build's output.
 */
static void parse_line(const char *line, size_t len, AseBuildDiagnostics *into) {
    /*
     * The separator that starts the numbers: ':' after a path, or '(' for
     * MSVC. Taken left to right, because `a.c:12:5:` read from the right
     * parses as line 5 of a file called `a.c:12`. Everything after it is
     * validated, so a Windows drive letter's colon simply fails and the
     * scan moves on to the '(' that follows.
     */
    const char *end = line + len;
    for (size_t at = 1; at < len; at++) {
        char c = line[at];
        if (c != ':' && c != '(') {
            continue;
        }
        /* A compiler leaves no space between the path and the line
         * number. "Time: 12:05" is not a diagnostic. */
        if (at + 1 >= len || line[at + 1] == ' ' || line[at + 1] == '\t') {
            continue;
        }

        const char *p = line + at + 1;
        char *after = NULL;

        long line_number = strtol(p, &after, 10);
        if (after == p || line_number <= 0) {
            continue;
        }
        p = after;

        long column_number = 0;
        if (p < end && (*p == ':' || *p == ',')) {
            char *after_column = NULL;
            long parsed = strtol(p + 1, &after_column, 10);
            if (after_column != p + 1 && parsed > 0) {
                column_number = parsed;
                p = after_column;
            }
        }

        if (c == '(') {
            if (p >= end || *p != ')') {
                continue;
            }
            p++;
        }
        if (p >= end || *p != ':') {
            continue;
        }
        p = skip_spaces(p + 1, end);

        const char *word = p;
        while (p < end && *p != ':' && *p != ' ') {
            p++;
        }
        int severity = severity_of(word, (size_t)(p - word));
        if (severity == 0) {
            continue;
        }

        /* MSVC puts its code between the word and the colon. */
        p = skip_spaces(p, end);
        while (p < end && *p != ':') {
            p++;
        }
        if (p >= end) {
            continue;
        }
        p = skip_spaces(p + 1, end);

        char *message = ase_memdup(p, (size_t)(end - p) + 1);
        if (message == NULL) {
            return;
        }
        message[end - p] = '\0';
        diagnostics_push(into, line, at, (int)line_number, (int)column_number, severity, message);
        free(message);
        return;
    }
}

AseBuildDiagnostics *ase_build_parse_output(const char *text, size_t len) {
    AseBuildDiagnostics *out = (AseBuildDiagnostics *)calloc(1, sizeof(AseBuildDiagnostics));
    if (out == NULL || text == NULL) {
        return out;
    }

    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i != len && text[i] != '\n') {
            continue;
        }
        size_t line_len = i - start;
        while (line_len > 0 && text[start + line_len - 1] == '\r') {
            line_len--;
        }
        if (line_len > 0) {
            parse_line(text + start, line_len, out);
        }
        start = i + 1;
    }
    return out;
}
