#pragma once

#include <astia/api/termios.h>

#ifndef TIOCGPTN
#define TIOCGPTN   0x80045430UL
#define TIOCSPTLCK 0x40045431UL
#define TIOCGPTLCK 0x80045439UL
#endif

int ioctl(int fd, unsigned long request, ...);
