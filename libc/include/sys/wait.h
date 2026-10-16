#pragma once

/*
 * astia libc — <sys/wait.h>
 *
 * Only the blocking form exists: options must be 0.  On success the
 * reaped pid is returned and *status (when non-null) receives the
 * child's exit status as passed to exit(); on failure -1 with errno.
 */

#ifdef __cplusplus
extern "C" {
#endif

int waitpid(int pid, int* status, int options);

#ifdef __cplusplus
}
#endif
