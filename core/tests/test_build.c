/*
 * Working out how to build a project nobody configured — see
 * docs/adr/0147. Inference only: nothing here runs a build, and
 * nothing in the module does either.
 */
#include "test_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define ASE_MKDIR(path) _mkdir(path)
#define ASE_RMDIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define ASE_MKDIR(path) mkdir(path, 0755)
#define ASE_RMDIR(path) rmdir(path)
#endif

#include "ase/build.h"

static const char *kRoot = "test_build_tree.tmp";

static void write_file(const char *path, const char *contents) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fputs(contents, f);
    fclose(f);
}

static void make_tree(void) {
    ASE_MKDIR(kRoot);
    ASE_MKDIR("test_build_tree.tmp/src");
    write_file("test_build_tree.tmp/src/main.c", "int main(void) { return 0; }\n");
}

static void remove_tree(void) {
    remove("test_build_tree.tmp/src/main.c");
    remove("test_build_tree.tmp/compile_commands.json");
    remove("test_build_tree.tmp/CMakeLists.txt");
    remove("test_build_tree.tmp/Makefile");
    remove("test_build_tree.tmp/Cargo.toml");
    remove("test_build_tree.tmp/build/CMakeCache.txt");
    ASE_RMDIR("test_build_tree.tmp/build");
    ASE_RMDIR("test_build_tree.tmp/src");
    ASE_RMDIR(kRoot);
}

/* Nothing recognisable is reported as nothing, never as something
 * plausible. */
static void test_an_unrecognised_tree_is_not_guessed_at(void) {
    make_tree();
    AseBuildGuess *guess = ase_build_infer("test_build_tree.tmp/src/main.c", kRoot);
    CHECK(guess == NULL);
    remove_tree();
}

/* The marker is found by walking up, not only beside the file. */
static void test_a_marker_above_the_file_is_found(void) {
    make_tree();
    write_file("test_build_tree.tmp/Makefile", "all:\n\techo hi\n");

    AseBuildGuess *guess = ase_build_infer("test_build_tree.tmp/src/main.c", kRoot);
    CHECK(guess != NULL);
    CHECK(strcmp(guess->command, "make") == 0);
    CHECK(strcmp(guess->directory, "test_build_tree.tmp") == 0);
    /* The evidence names the file that decided it, so the guess can be
     * checked rather than trusted. */
    CHECK(strstr(guess->evidence, "Makefile") != NULL);
    CHECK(!guess->single_file);

    ase_build_guess_destroy(guess);
    remove_tree();
}

/* A configured build directory beats the file that would configure
 * one: running cmake -B over an existing build is the slower answer to
 * the same question. */
static void test_a_configured_build_dir_wins(void) {
    make_tree();
    write_file("test_build_tree.tmp/CMakeLists.txt", "project(x)\n");

    AseBuildGuess *first = ase_build_infer("test_build_tree.tmp/src/main.c", kRoot);
    CHECK(first != NULL);
    CHECK(strcmp(first->command, "cmake -B build && cmake --build build") == 0);
    ase_build_guess_destroy(first);

    ASE_MKDIR("test_build_tree.tmp/build");
    write_file("test_build_tree.tmp/build/CMakeCache.txt", "CMAKE_PROJECT_NAME:STATIC=x\n");

    AseBuildGuess *second = ase_build_infer("test_build_tree.tmp/src/main.c", kRoot);
    CHECK(second != NULL);
    CHECK(strcmp(second->command, "cmake --build build") == 0);
    ase_build_guess_destroy(second);

    remove_tree();
}

/* The build system's own answer for this file beats any guess about
 * the project. */
static void test_compile_commands_wins_over_a_marker(void) {
    make_tree();
    write_file("test_build_tree.tmp/Makefile", "all:\n\techo hi\n");
    write_file("test_build_tree.tmp/compile_commands.json",
               "[{\"directory\": \"/tmp/build\","
               " \"command\": \"cc -c -o main.o main.c\","
               " \"file\": \"test_build_tree.tmp/src/main.c\"}]\n");

    AseBuildGuess *guess = ase_build_infer("test_build_tree.tmp/src/main.c", kRoot);
    CHECK(guess != NULL);
    CHECK(strcmp(guess->command, "cc -c -o main.o main.c") == 0);
    CHECK(strcmp(guess->directory, "/tmp/build") == 0);
    /* Said out loud, because it builds one file and the marker would
     * have built the project. */
    CHECK(guess->single_file);

    ase_build_guess_destroy(guess);
    remove_tree();
}

/* Both shapes are in the spec: cmake writes `command`, others write
 * `arguments`. */
static void test_compile_commands_arguments_form(void) {
    make_tree();
    write_file("test_build_tree.tmp/compile_commands.json",
               "[{\"directory\": \"/tmp/build\","
               " \"arguments\": [\"cc\", \"-c\", \"main.c\"],"
               " \"file\": \"test_build_tree.tmp/src/main.c\"}]\n");

    AseBuildGuess *guess = ase_build_infer("test_build_tree.tmp/src/main.c", kRoot);
    CHECK(guess != NULL);
    CHECK(strcmp(guess->command, "cc -c main.c") == 0);

    ase_build_guess_destroy(guess);
    remove_tree();
}

/* A database that does not mention this file falls through to the
 * project marker rather than picking somebody else's entry. */
static void test_a_database_without_this_file_falls_through(void) {
    make_tree();
    write_file("test_build_tree.tmp/Makefile", "all:\n\techo hi\n");
    write_file("test_build_tree.tmp/compile_commands.json",
               "[{\"directory\": \"/tmp/build\","
               " \"command\": \"cc -c other.c\","
               " \"file\": \"/somewhere/else/other.c\"}]\n");

    AseBuildGuess *guess = ase_build_infer("test_build_tree.tmp/src/main.c", kRoot);
    CHECK(guess != NULL);
    CHECK(strcmp(guess->command, "make") == 0);

    ase_build_guess_destroy(guess);
    remove_tree();
}

static void test_malformed_database_is_not_fatal(void) {
    make_tree();
    write_file("test_build_tree.tmp/Makefile", "all:\n\techo hi\n");
    write_file("test_build_tree.tmp/compile_commands.json", "{ this is not json\n");

    AseBuildGuess *guess = ase_build_infer("test_build_tree.tmp/src/main.c", kRoot);
    CHECK(guess != NULL);
    CHECK(strcmp(guess->command, "make") == 0);

    ase_build_guess_destroy(guess);
    remove_tree();
}

/*
 * The nearest marker is usually the wrong one. A CMakeLists.txt in a
 * subdirectory describes a component; the project is further up.
 */
static void test_the_outermost_marker_wins(void) {
    make_tree();
    write_file("test_build_tree.tmp/CMakeLists.txt", "project(x)\n");
    write_file("test_build_tree.tmp/src/CMakeLists.txt", "add_library(y y.c)\n");

    AseBuildGuess *guess = ase_build_infer("test_build_tree.tmp/src/main.c", kRoot);
    CHECK(guess != NULL);
    /* The root's, not the one sitting beside the file. */
    CHECK(strcmp(guess->directory, "test_build_tree.tmp") == 0);
    ase_build_guess_destroy(guess);

    remove("test_build_tree.tmp/src/CMakeLists.txt");
    remove_tree();
}

/* Without a bound the walk reaches a home directory with somebody
 * else's Makefile in it, so a repository stops it. */
static void test_a_repository_bounds_the_walk(void) {
    make_tree();
    write_file("test_build_tree.tmp/Makefile", "all:\n\techo hi\n");
    ASE_MKDIR("test_build_tree.tmp/.git");

    /* No stop_at at all: the .git is what has to stop it, and the
     * answer must still be the one inside the repository. */
    AseBuildGuess *guess = ase_build_infer("test_build_tree.tmp/src/main.c", NULL);
    CHECK(guess != NULL);
    CHECK(strcmp(guess->command, "make") == 0);
    CHECK(strcmp(guess->directory, "test_build_tree.tmp") == 0);
    ase_build_guess_destroy(guess);

    ASE_RMDIR("test_build_tree.tmp/.git");
    remove_tree();
}

static void test_nonsense_arguments(void) {
    CHECK(ase_build_infer(NULL, NULL) == NULL);
    ase_build_guess_destroy(NULL);

    /* Every marker has a name and a command, and the table the help
     * text reads is the one the search uses. */
    CHECK(ase_build_marker_count() > 0);
    for (size_t i = 0; i < ase_build_marker_count(); i++) {
        CHECK(ase_build_marker_name(i) != NULL && ase_build_marker_name(i)[0] != '\0');
        CHECK(ase_build_marker_command(i) != NULL && ase_build_marker_command(i)[0] != '\0');
    }
    CHECK(ase_build_marker_name(ase_build_marker_count()) == NULL);
    CHECK(ase_build_marker_command(ase_build_marker_count()) == NULL);
}

int main(void) {
    RUN(test_an_unrecognised_tree_is_not_guessed_at);
    RUN(test_a_marker_above_the_file_is_found);
    RUN(test_a_configured_build_dir_wins);
    RUN(test_compile_commands_wins_over_a_marker);
    RUN(test_compile_commands_arguments_form);
    RUN(test_a_database_without_this_file_falls_through);
    RUN(test_the_outermost_marker_wins);
    RUN(test_a_repository_bounds_the_walk);
    RUN(test_malformed_database_is_not_fatal);
    RUN(test_nonsense_arguments);

    printf("all build inference tests passed\n");
    return 0;
}
