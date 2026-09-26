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
