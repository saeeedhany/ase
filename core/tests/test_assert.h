#ifndef ASE_TEST_ASSERT_H
#define ASE_TEST_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <crtdbg.h>
#include <windows.h>
#endif

/*
 * A failing test on Windows has to print and exit, not open a window.
 *
 * abort() under the Debug CRT pops a modal "Debug Error!" box, and a
 * crash pops Windows Error Reporting. On a CI runner nobody clicks
 * either, so the job hangs until it times out — which is how the first
 * real failure after the internal.h fix presented: a suite that used to
 * segfault in three seconds instead ran for twenty minutes.
 *
 * Idempotent and called from RUN, so it covers a crash as well as a
 * failed CHECK. See docs/adr/0143.
 */
static inline void ase_test_no_dialogs(void) {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
}

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
#define RUN(fn)                                                                                    \
    do {                                                                                           \
        ase_test_no_dialogs();                                                                     \
        fprintf(stderr, "  %s\n", #fn);                                                            \
        fflush(stderr);                                                                            \
        fn();                                                                                      \
    } while (0)

#endif /* ASE_TEST_ASSERT_H */
