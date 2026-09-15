#ifndef ASE_PROCESS_H
#define ASE_PROCESS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A spawned child process with stdin/stdout on pipes, shared by the LSP
 * client and :compile. POSIX-real, Windows-stub: spawn returns NULL
 * cleanly there rather than shipping untested async I/O.
 */
typedef struct AseProcess AseProcess;

/* NULL-terminated argv; cwd may be NULL. Equivalent to
 * ase_process_spawn_ex(command, cwd, true) — stderr merged, which is
 * what :compile wants. */
AseProcess *ase_process_spawn(const char *const *command, const char *cwd);

/* stderr merging optional. A real LSP server logs to stderr, and
 * merging that into the framed JSON-RPC stream corrupts Content-Length
 * framing, breaking every response after the first log line. With
 * merge_stderr=false the child's stderr is inherited, not piped. */
AseProcess *ase_process_spawn_ex(const char *const *command, const char *cwd, bool merge_stderr);

/* Non-blocking. Returns bytes read, 0 on EOF, or -1 both for "nothing
 * available yet" and for a real error — check ase_process_has_exited()
 * to tell those apart. */
long ase_process_read(AseProcess *process, char *buf, size_t cap);

/* Blocking write of the full buffer (retries on partial writes/EINTR).
 * Returns false on any real error (e.g. EPIPE — the process died). */
bool ase_process_write(AseProcess *process, const char *data, size_t len);

/* Checked non-blockingly on every call. True once the child has
 * exited. */
/* Like ase_process_destroy, but does not wait for the child to die:
 * the pipes are closed, SIGTERM is sent, and the pid is remembered so a
 * later spawn or destroy can reap it. For a process whose exit status
 * nobody wants — a language server being discarded — where waiting cost
 * tens of milliseconds on the UI thread. See docs/adr/0092. */
void ase_process_destroy_detached(AseProcess *process);

bool ase_process_has_exited(AseProcess *process);
/* Valid only once ase_process_has_exited() is true. */
int ase_process_exit_code(AseProcess *process);

/* Grace period then SIGKILL, closes the pipes, frees `process`. */
void ase_process_destroy(AseProcess *process);

#ifdef __cplusplus
}
#endif

#endif /* ASE_PROCESS_H */
