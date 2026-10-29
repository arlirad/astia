#pragma once

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status) ((status) & 0x7f)
#define WCOREDUMP(status) ((status) & 0x80)
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status) (((status) >> 8) & 0xff)
#define WIFCONTINUED(status) ((status) == 0xffff)

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
