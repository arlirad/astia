#pragma once

#define WNOHANG 1
#define WUNTRACED 2

int waitpid(int pid, int* status, int options);

/*
 * astia libc — <sys/wait.h>
 *
 * The Astia syscall provides the blocking form.  WNOHANG is accepted by
 * the libc compatibility wrapper but cannot observe completion until the
 * syscall ABI carries wait options.  On success the reaped pid is returned
 * and *status (when non-null) receives the child's exit status as passed to
 * exit(); on failure -1 with errno.
 */

#ifdef __cplusplus
extern "C" {
#endif

int waitpid(int pid, int* status, int options);

#ifdef __cplusplus
}
#endif
