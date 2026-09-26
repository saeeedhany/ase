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

/* ------------------------------------------------- reading the output back */

static const AseBuildDiagnostic *only(AseBuildDiagnostics *set) {
    CHECK(ase_build_diagnostic_count(set) == 1);
    return ase_build_diagnostic_at(set, 0);
}

static void test_gcc_and_clang_lines(void) {
    const char *text =
        "src/main.c:12:5: error: expected ';' before '}' token\n"
        "src/main.c:20: warning: unused variable 'x'\n"
        "src/other.c:3:1: note: declared here\n";
    AseBuildDiagnostics *set = ase_build_parse_output(text, strlen(text));
    CHECK(ase_build_diagnostic_count(set) == 3);

    const AseBuildDiagnostic *first = ase_build_diagnostic_at(set, 0);
    CHECK(strcmp(first->file, "src/main.c") == 0);
    CHECK(first->line == 12 && first->column == 5 && first->severity == 1);
    CHECK(strcmp(first->message, "expected ';' before '}' token") == 0);

    /* No column printed is 0, not 1: the caller can tell the difference
     * between "column one" and "did not say". */
    const AseBuildDiagnostic *second = ase_build_diagnostic_at(set, 1);
    CHECK(second->line == 20 && second->column == 0 && second->severity == 2);

    CHECK(ase_build_diagnostic_at(set, 2)->severity == 3);
    CHECK(ase_build_diagnostic_at(set, 3) == NULL);
    ase_build_diagnostics_destroy(set);
}

static void test_msvc_lines(void) {
    const char *text =
        "C:\\src\\main.c(12,5): error C2065: 'x': undeclared identifier\r\n"
        "C:\\src\\main.c(20): warning C4101: unreferenced local variable\r\n";
    AseBuildDiagnostics *set = ase_build_parse_output(text, strlen(text));
    CHECK(ase_build_diagnostic_count(set) == 2);

    const AseBuildDiagnostic *first = ase_build_diagnostic_at(set, 0);
    /* The drive letter's colon must not be mistaken for the separator. */
    CHECK(strcmp(first->file, "C:\\src\\main.c") == 0);
    CHECK(first->line == 12 && first->column == 5 && first->severity == 1);
    CHECK(strcmp(first->message, "'x': undeclared identifier") == 0);

    const AseBuildDiagnostic *second = ase_build_diagnostic_at(set, 1);
    CHECK(second->line == 20 && second->column == 0 && second->severity == 2);
    ase_build_diagnostics_destroy(set);
}

/* Most of a build's output is not a diagnostic, and none of it may be
 * mistaken for one. */
static void test_ordinary_output_is_not_a_diagnostic(void) {
    const char *text =
        "make: Entering directory '/home/me/project'\n"
        "[ 50%] Building C object CMakeFiles/x.dir/main.c.o\n"
        "gcc -O2 -o x main.c\n"
        "Time: 12:05:33\n"
        "\n"
        "make: *** [Makefile:7: all] Error 1\n";
    AseBuildDiagnostics *set = ase_build_parse_output(text, strlen(text));
    CHECK(ase_build_diagnostic_count(set) == 0);
    ase_build_diagnostics_destroy(set);
}

/* clang says "fatal error", which is still an error. */
static void test_fatal_error(void) {
    const char *text = "src/a.c:1:10: fatal error: 'nope.h' file not found\n";
    AseBuildDiagnostics *set = ase_build_parse_output(text, strlen(text));
    const AseBuildDiagnostic *item = only(set);
    CHECK(item->severity == 1);
    CHECK(strcmp(item->message, "'nope.h' file not found") == 0);
    ase_build_diagnostics_destroy(set);
}

/* A message with colons in it keeps all of them. */
static void test_a_message_containing_colons(void) {
    const char *text = "a.c:5:1: error: cannot convert 'int' to 'char*': no known conversion\n";
    AseBuildDiagnostics *set = ase_build_parse_output(text, strlen(text));
    const AseBuildDiagnostic *item = only(set);
    CHECK(strcmp(item->file, "a.c") == 0);
    CHECK(item->line == 5);
    CHECK(strcmp(item->message, "cannot convert 'int' to 'char*': no known conversion") == 0);
    ase_build_diagnostics_destroy(set);
}

static void test_empty_and_nonsense_output(void) {
    AseBuildDiagnostics *set = ase_build_parse_output("", 0);
    CHECK(set != NULL && ase_build_diagnostic_count(set) == 0);
    ase_build_diagnostics_destroy(set);

    set = ase_build_parse_output(NULL, 0);
    ase_build_diagnostics_destroy(set);

    CHECK(ase_build_diagnostic_count(NULL) == 0);
    CHECK(ase_build_diagnostic_at(NULL, 0) == NULL);
    ase_build_diagnostics_destroy(NULL);
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

    RUN(test_gcc_and_clang_lines);
    RUN(test_msvc_lines);
    RUN(test_ordinary_output_is_not_a_diagnostic);
    RUN(test_fatal_error);
    RUN(test_a_message_containing_colons);
    RUN(test_empty_and_nonsense_output);

    printf("all build tests passed\n");
    return 0;
}
