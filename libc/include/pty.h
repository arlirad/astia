#pragma once

#include <sys/types.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

int openpty(int* master, int* slave, char* name, const struct termios* termp,
            const struct winsize* winp);

#ifdef __cplusplus
}
#endif
