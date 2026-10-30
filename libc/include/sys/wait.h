#pragma once

#include <sys/types.h>
#include <astia/api/wait.h>

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8
#define WIFEXITED(status) (((status) & ASTIA_WAIT_SIGNAL_MASK) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & ASTIA_WAIT_EXIT_MASK)
#define WIFSIGNALED(status) (((status) & ASTIA_WAIT_SIGNAL_MASK) != 0 && \
                             ((status) & ASTIA_WAIT_SIGNAL_MASK) != ASTIA_WAIT_STOPPED_MARKER)
#define WTERMSIG(status) ((status) & ASTIA_WAIT_SIGNAL_MASK)
#define WCOREDUMP(status) ((status) & ASTIA_WAIT_CORE_BIT)
#define WIFSTOPPED(status) (((status) & ASTIA_WAIT_EXIT_MASK) == ASTIA_WAIT_STOPPED_MARKER)
#define WSTOPSIG(status) (((status) >> 8) & ASTIA_WAIT_EXIT_MASK)
#define WIFCONTINUED(status) ((status) == ASTIA_WAIT_CONTINUED_MARKER)

int waitpid(int pid, int* status, int options);
int wait(int* status);

/*
 * astia libc — <sys/wait.h>
 *
 * SYS_WAITPID retains its original number and appends an options argument.
 * Status values use the traditional POSIX bit layout.
 */

#ifdef __cplusplus
extern "C" {
#endif

int waitpid(int pid, int* status, int options);
int wait(int* status);

#ifdef __cplusplus
}
#endif
