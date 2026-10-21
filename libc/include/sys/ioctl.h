#pragma once

#include <stddef.h>

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCSPGRP 0x5410
#define TIOCGPGRP 0x540f
#define TCGETS 0x5401
#define TCSETS 0x5402

int ioctl(int fd, unsigned long request, ...);
