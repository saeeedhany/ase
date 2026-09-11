#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ase/process.h"

/* Polls until `predicate` is true or `timeout_ms` elapses (10ms steps,
 * same shape as lsp_client's own wait_for). Returns true if the
 * predicate became true before timing out. */
static bool poll_until(bool (*predicate)(void *), void *arg, int timeout_ms) {
    int elapsed = 0;
    while (!predicate(arg) && elapsed < timeout_ms) {
        usleep(10 * 1000);
        elapsed += 10;
    }
    return predicate(arg);
}

static bool exited(void *arg) {
    return ase_process_has_exited((AseProcess *)arg);
}

static void test_echo_output(void) {
    const char *command[] = {"/bin/echo", "hello from ase_process", NULL};
    AseProcess *process = ase_process_spawn(command, NULL);
    assert(process != NULL);

    assert(poll_until(exited, process, 2000));
    assert(ase_process_exit_code(process) == 0);

    char buf[256];
    long n = ase_process_read(process, buf, sizeof(buf) - 1);
    assert(n > 0);
    buf[n] = '\0';
    assert(strstr(buf, "hello from ase_process") != NULL);

    ase_process_destroy(process);
}

static void test_exit_code_nonzero(void) {
    const char *command[] = {"/bin/sh", "-c", "exit 7", NULL};
    AseProcess *process = ase_process_spawn(command, NULL);
    assert(process != NULL);

    assert(poll_until(exited, process, 2000));
    assert(ase_process_exit_code(process) == 7);

    ase_process_destroy(process);
}

static void test_cwd(void) {
    const char *command[] = {"/bin/pwd", NULL};
    AseProcess *process = ase_process_spawn(command, "/tmp");
    assert(process != NULL);

    assert(poll_until(exited, process, 2000));
    assert(ase_process_exit_code(process) == 0);

    char buf[256];
    long n = ase_process_read(process, buf, sizeof(buf) - 1);
    assert(n > 0);
    buf[n] = '\0';
    /* /tmp is a symlink to /private/tmp on macOS, and plain /bin/pwd
     * (invoked directly, no shell, so no logical $PWD to fall back to)
     * calls getcwd(), which always returns the symlink-resolved
     * physical path — so asserting the literal string "/tmp" is
     * platform-dependent and was failing CI's macos-latest job.
     * realpath() resolves the same way pwd's getcwd() does, so
     * comparing against that instead holds on every platform. */
    char resolved_tmp[PATH_MAX];
    assert(realpath("/tmp", resolved_tmp) != NULL);
    size_t resolved_len = strlen(resolved_tmp);
    assert(strncmp(buf, resolved_tmp, resolved_len) == 0);

    ase_process_destroy(process);
}

static void test_write_roundtrip(void) {
    const char *command[] = {"/bin/cat", NULL};
    AseProcess *process = ase_process_spawn(command, NULL);
    assert(process != NULL);

    const char *message = "round trip through cat\n";
    assert(ase_process_write(process, message, strlen(message)));

    char buf[256];
    long total = 0;
    int elapsed = 0;
    while (total < (long)strlen(message) && elapsed < 2000) {
        long n = ase_process_read(process, buf + total, sizeof(buf) - (size_t)total);
        if (n > 0) {
            total += n;
            continue;
        }
        usleep(10 * 1000);
        elapsed += 10;
    }
    assert(total == (long)strlen(message));
    assert(memcmp(buf, message, (size_t)total) == 0);

    ase_process_destroy(process);
}

static void test_bad_command_exits_nonzero(void) {
    /* fork() succeeds; execvp() fails inside the child — spawn itself
     * must still succeed, with the failure only observable via the
     * exit code (127, matching the shell convention this project's
     * _exit(127) on execvp failure already follows). */
    const char *command[] = {"/no/such/executable-ase-test", NULL};
    AseProcess *process = ase_process_spawn(command, NULL);
    assert(process != NULL);

    assert(poll_until(exited, process, 2000));
    assert(ase_process_exit_code(process) == 127);

    ase_process_destroy(process);
}

static void test_null_command_rejected(void) {
    assert(ase_process_spawn(NULL, NULL) == NULL);
    const char *empty[] = {NULL};
    assert(ase_process_spawn(empty, NULL) == NULL);
}

/* The bug this covers: a real LSP server logs to stderr, and if that
 * lands in the same pipe as the framed JSON-RPC stdout stream it
 * corrupts the Content-Length framing — see docs/adr/0029. Proves both
 * directions: merge_stderr=true still merges (ase_process_spawn's
 * existing, unchanged behavior — used by :compile), merge_stderr=false
 * keeps stderr out of the readable stream entirely. */
static void test_stderr_merge_toggle(void) {
    const char *command[] = {"/bin/sh", "-c", "echo to-stdout; echo to-stderr 1>&2", NULL};

    AseProcess *merged = ase_process_spawn_ex(command, NULL, true);
    assert(merged != NULL);
    assert(poll_until(exited, merged, 2000));
    char merged_buf[256];
    long merged_n = ase_process_read(merged, merged_buf, sizeof(merged_buf) - 1);
    assert(merged_n > 0);
    merged_buf[merged_n] = '\0';
    assert(strstr(merged_buf, "to-stdout") != NULL);
    assert(strstr(merged_buf, "to-stderr") != NULL);
    ase_process_destroy(merged);

    AseProcess *separated = ase_process_spawn_ex(command, NULL, false);
    assert(separated != NULL);
    assert(poll_until(exited, separated, 2000));
    char separated_buf[256];
    long separated_n = ase_process_read(separated, separated_buf, sizeof(separated_buf) - 1);
    assert(separated_n > 0);
    separated_buf[separated_n] = '\0';
    assert(strstr(separated_buf, "to-stdout") != NULL);
    assert(strstr(separated_buf, "to-stderr") == NULL);
    ase_process_destroy(separated);
}

int main(void) {
    test_echo_output();
    test_exit_code_nonzero();
    test_cwd();
    test_write_roundtrip();
    test_bad_command_exits_nonzero();
    test_null_command_rejected();
    test_stderr_merge_toggle();
    printf("test_process: all tests passed\n");
    return 0;
}
