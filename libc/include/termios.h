#pragma once

#include <astia/api/termios.h>

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#ifdef __cplusplus
extern "C" {
#endif

int tcgetattr(int fd, struct termios* termios_p);
int tcsetattr(int fd, int actions, const struct termios* termios_p);
int tcsendbreak(int fd, int duration);
int tcdrain(int fd);
int tcflush(int fd, int queue_selector);
int tcflow(int fd, int action);
speed_t cfgetispeed(const struct termios* termios_p);
speed_t cfgetospeed(const struct termios* termios_p);
int cfsetispeed(struct termios* termios_p, speed_t speed);
int cfsetospeed(struct termios* termios_p, speed_t speed);

#ifdef __cplusplus
}
#endif
