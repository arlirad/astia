#pragma once

/*
 * astia libc — <unistd.h> (process identity subset)
 *
 * Thin shims over the direct syscalls; <astia/api/syscalls.h> remains
 * the raw ABI.  Negative kernel codes come back as -1 with errno.
 */

#ifdef __cplusplus
extern "C" {
#endif

int getpid(void);
int gettid(void);

/* Create a process from the executable at `path` with the given
 * NULL-terminated argv/envp (a null vector means empty).  Returns the
 * child pid > 0, or -1 with errno.  Creates nothing on failure; pair
 * the success case with waitpid(). */
int spawnve(const char* path, char* const argv[], char* const envp[]);

/* Duplicate this process copy-on-write: every mapping is shared until
 * either side writes it, and the child resumes at fork()'s return
 * with the value 0.  Returns the child pid in the parent, 0 in the
 * child, -1 with errno on failure. */
int fork(void);

#ifdef __cplusplus
}
#endif
