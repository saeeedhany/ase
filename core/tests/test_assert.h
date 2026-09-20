#ifndef ASE_TEST_ASSERT_H
#define ASE_TEST_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

/*
 * The check every test in this project uses. Not <assert.h> — see
 * docs/adr/0064.
 *
 * `assert()` is compiled out when NDEBUG is defined, which CMake does
 * for every Release build, which is what the packages ship. That made
 * the entire suite **vacuous in Release**: 263 checks across nine
 * binaries, none of them evaluated.
 *
 * Worse than vacuous, in fact. 122 of those checks wrapped a call with
 * side effects — `assert(ase_undo_undo(undo, buf, &cursors, &count))`
 * being the shape — so under NDEBUG the call itself disappeared, the
 * out-parameters it was supposed to fill stayed uninitialised, and the
 * next line freed or read them. Three suites segfaulted outright; the
 * rest passed while testing nothing.
 *
 * This macro always evaluates its expression, in every build type, and
 * says which line failed. A test that quietly stops testing in the
 * configuration you ship is worse than having no test at all: it still
 * reports success.
 */
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr);                \
            fflush(stderr);                                                                        \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

/*
 * Names each case before it runs, on stderr and flushed.
 *
 * Three of these suites segfaulted on their first Windows run with no
 * output at all — a crash prints nothing, so "which test" was not
 * knowable from a CI log on a platform nobody here can attach a
 * debugger to. The last name printed is the one that died.
 * See docs/adr/0130.
 */
/* Temporary, for pinning down a crash on a platform with no debugger
 * to hand. Remove once Windows is green. */
#define TRACE(...)                                                                                 \
    do {                                                                                           \
        fprintf(stderr, "    [trace] " __VA_ARGS__);                                               \
        fprintf(stderr, "\n");                                                                     \
        fflush(stderr);                                                                            \
    } while (0)

#define RUN(fn)                                                                                    \
    do {                                                                                           \
        fprintf(stderr, "  %s\n", #fn);                                                            \
        fflush(stderr);                                                                            \
        fn();                                                                                      \
    } while (0)

#endif /* ASE_TEST_ASSERT_H */
