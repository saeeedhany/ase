#include "ase/process.h"

#include <stdlib.h>

#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

struct AseProcess {
    long pid;
    int read_fd;
    int write_fd;
    bool exited;
    int exit_code;
};

#if defined(_WIN32)

/* Not implemented — same reasoning as the LSP client's own Windows
 * stub (docs/adr/0011 decision 6): async child-process I/O on Windows
 * needs overlapped I/O or a reader thread, neither attempted here.
 * ase_process_spawn() returns NULL cleanly instead of spawning
 * anything; every function below is consequently unreachable on
 * Windows (every entry point checks for NULL first) but must still
 * exist to link. */

AseProcess *ase_process_spawn(const char *const *command, const char *cwd) {
    (void)command;
    (void)cwd;
    return NULL;
}

long ase_process_read(AseProcess *process, char *buf, size_t cap) {
    (void)process;
    (void)buf;
    (void)cap;
    return -1;
}

bool ase_process_write(AseProcess *process, const char *data, size_t len) {
    (void)process;
    (void)data;
    (void)len;
    return false;
}

bool ase_process_has_exited(AseProcess *process) {
    return process != NULL && process->exited;
}

int ase_process_exit_code(AseProcess *process) {
    return (process != NULL) ? process->exit_code : -1;
}

void ase_process_destroy(AseProcess *process) {
    free(process);
}

#else /* POSIX */

static bool platform_spawn(const char *const *command, const char *cwd, long *out_pid, int *out_read_fd,
                            int *out_write_fd) {
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) != 0) {
        return false;
    }
    if (pipe(stdout_pipe) != 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return false;
    }

    if (pid == 0) {
        /* child */
        if (cwd != NULL && chdir(cwd) != 0) {
            _exit(127); /* wrong directory is actively misleading — don't run anyway */
        }
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO); /* merged with stdout — see ase_process_read's doc comment */
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        execvp(command[0], (char *const *)command);
        _exit(127); /* execvp failed */
    }

    /* parent */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    *out_pid = (long)pid;
    *out_write_fd = stdin_pipe[1];
    *out_read_fd = stdout_pipe[0];
    return true;
}

static void platform_terminate(long pid, int read_fd, int write_fd) {
    if (write_fd >= 0) {
        close(write_fd);
    }
    if (read_fd >= 0) {
        close(read_fd);
    }

    if (pid > 0) {
        pid_t p = (pid_t)pid;
        int status;
        if (waitpid(p, &status, WNOHANG) == 0) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 200L * 1000000L;
            nanosleep(&ts, NULL);
            if (waitpid(p, &status, WNOHANG) == 0) {
                kill(p, SIGKILL);
                waitpid(p, &status, 0);
            }
        }
    }
}

AseProcess *ase_process_spawn(const char *const *command, const char *cwd) {
    if (command == NULL || command[0] == NULL) {
        return NULL;
    }

    signal(SIGPIPE, SIG_IGN); /* see docs/adr/0011 */

    long pid;
    int read_fd;
    int write_fd;
    if (!platform_spawn(command, cwd, &pid, &read_fd, &write_fd)) {
        return NULL;
    }

    AseProcess *process = (AseProcess *)calloc(1, sizeof(AseProcess));
    if (process == NULL) {
        platform_terminate(pid, read_fd, write_fd);
        return NULL;
    }

    process->pid = pid;
    process->read_fd = read_fd;
    process->write_fd = write_fd;
    return process;
}

long ase_process_read(AseProcess *process, char *buf, size_t cap) {
    if (process == NULL) {
        return -1;
    }
    return (long)read(process->read_fd, buf, cap);
}

bool ase_process_write(AseProcess *process, const char *data, size_t len) {
    if (process == NULL) {
        return false;
    }
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(process->write_fd, data + written, len - written);
        if (n > 0) {
            written += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false; /* EPIPE (process died) or any other error */
    }
    return true;
}

bool ase_process_has_exited(AseProcess *process) {
    if (process == NULL) {
        return false;
    }
    if (process->exited) {
        return true;
    }
    int status;
    pid_t result = waitpid((pid_t)process->pid, &status, WNOHANG);
    if (result > 0) {
        process->exited = true;
        process->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return process->exited;
}

int ase_process_exit_code(AseProcess *process) {
    return (process != NULL) ? process->exit_code : -1;
}

void ase_process_destroy(AseProcess *process) {
    if (process == NULL) {
        return;
    }
    platform_terminate(process->pid, process->read_fd, process->write_fd);
    free(process);
}

#endif
