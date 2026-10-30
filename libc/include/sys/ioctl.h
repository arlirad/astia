#pragma once

#include <astia/api/termios.h>

int ioctl(int fd, unsigned long request, ...);
