#ifndef ASE_PROCESS_H
#define ASE_PROCESS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A spawned child process with its stdin/stdout connected via pipes —
 * extracted from the LSP client's process-spawning code (see
 * docs/adr/0011) so :compile (docs/adr/0025) can reuse it instead of
 * duplicating it; ase_lsp_client_start() is now a consumer of this
 * module rather than having its own copy. Same POSIX-real/Windows-stub
 * split as the code it came from: ase_process_spawn() returns NULL
 * cleanly on Windows rather than shipping untested async-I/O code, per
 * ADR 0011 decision 6.
 */
typedef struct AseProcess AseProcess;

/* command[0] is the executable; command is a NULL-terminated argv-style
 * array. cwd may be NULL (inherit the caller's working directory).
 * Returns NULL if the process couldn't be spawned (or on Windows,
 * always). */
AseProcess *ase_process_spawn(const char *const *command, const char *cwd);

/* Non-blocking: reads whatever stdout bytes (stderr is redirected into
 * the same stream — v1 doesn't separate them, matching the LSP
 * client's own stdout-only framing) are currently available into
 * `buf`, up to `cap`. Returns the number of bytes read, 0 on EOF (the
 * process closed its output, though it may not have exited yet), or
 * -1 if nothing is available right now (not an error — poll again
 * later) or on a real read error (check ase_process_has_exited() to
 * tell the two apart). */
long ase_process_read(AseProcess *process, char *buf, size_t cap);

/* Blocking write of the full buffer (retries on partial writes/EINTR).
 * Returns false on any real error (e.g. EPIPE — the process died). */
bool ase_process_write(AseProcess *process, const char *data, size_t len);

/* Checked non-blockingly on every call. True once the child has
 * exited. */
bool ase_process_has_exited(AseProcess *process);
/* Valid only once ase_process_has_exited() is true. */
int ase_process_exit_code(AseProcess *process);

/* Terminates the process if it's still running (same grace-period-
 * then-SIGKILL sequence the LSP client's shutdown already used),
 * closes its pipes, and frees `process`. */
void ase_process_destroy(AseProcess *process);

#ifdef __cplusplus
}
#endif

#endif /* ASE_PROCESS_H */
