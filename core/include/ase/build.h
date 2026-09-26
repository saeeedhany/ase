#ifndef ASE_BUILD_H
#define ASE_BUILD_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Working out how to build a project nobody configured — see
 * docs/adr/0147.
 *
 * Inference only. Nothing here runs anything: the caller shows what was
 * found and lets a person agree to it, because a misdetected build
 * target running by itself is worse than being asked.
 */

typedef struct {
    /* The shell command, and the directory to run it in. */
    char *command;
    char *directory;
    /* The file that decided it, absolute — what makes the guess
     * checkable rather than magic. */
    char *evidence;
    /* True when the command builds only `file_path`, from a
     * compile_commands.json entry, rather than the whole project. */
    bool single_file;
} AseBuildGuess;

/*
 * Looks for a way to build `file_path`, walking up from the directory
 * holding it. `file_path` should be absolute: the walk goes up by
 * trimming components, so a relative path runs out at its own first one.
 *
 * It stops at `stop_at` (inclusive), at a directory holding `.git`, or
 * at the filesystem root — whichever comes first. `stop_at` may be NULL.
 *
 * A compile_commands.json with an entry for this exact file wins: it is
 * the build system's own answer rather than a guess about it. Otherwise
 * the first recognised project marker going up.
 *
 * NULL when nothing is recognised, which the caller reports — this
 * never falls back to something plausible.
 */
AseBuildGuess *ase_build_infer(const char *file_path, const char *stop_at);

void ase_build_guess_destroy(AseBuildGuess *guess);

/* The markers, in the order they are tried. Exposed so the help text
 * and the tests describe the same list rather than two copies of it. */
size_t ase_build_marker_count(void);
const char *ase_build_marker_name(size_t index);
const char *ase_build_marker_command(size_t index);

#ifdef __cplusplus
}
#endif

#endif /* ASE_BUILD_H */
