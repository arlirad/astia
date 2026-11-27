#pragma once

#include <sys/types.h>

#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimum capacity, including the terminating NUL, that openpty() requires
 * for its `name` buffer.  openpty() writes at most this many bytes and fails
 * with ERANGE instead of truncating a name that does not fit. */
#define PTY_NAME_MAX 32

int openpty(int* master, int* slave, char* name, const struct termios* termp,
            const struct winsize* winp);

#ifdef __cplusplus
}
#endif
