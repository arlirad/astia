#pragma once

/*
 * astia/api/termios.h — terminal line discipline state.
 *
 * The struct a tty carries and the TCGETS/TCSETS ioctls that read and
 * replace it, shaped after <termios.h>.  Flag and speed values follow
 * glibc numbering so libc's tcgetattr/tcsetattr will pass them through
 * untouched. The tty implements raw/canonical input, echo, basic input/output
 * translations, software flow control, line editing and VMIN=0/nonzero.
 * Signal delivery and VTIME still require the kernel signal/timer services.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NCCS 11

/* c_cc indexes */
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSWTC 7
#define VSTART 8
#define VSTOP 9
#define VSUSP 10

typedef unsigned int  tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int  speed_t;

struct termios {
    tcflag_t c_iflag;    /* input modes */
    tcflag_t c_oflag;    /* output modes */
    tcflag_t c_cflag;    /* control modes */
    tcflag_t c_lflag;    /* local modes */
    cc_t     c_line;     /* line discipline; N_TTY only */
    cc_t     c_cc[NCCS]; /* special characters (uninterpreted) */
    speed_t  c_ispeed;   /* input baud rate, POSIX speed index */
    speed_t  c_ospeed;   /* output baud rate, POSIX speed index */
};

/* Terminal window size, as used by TIOCGWINSZ/TIOCSWINSZ. */
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

/* c_iflag */
#define IGNBRK 0x000001
#define BRKINT 0x000002
#define IGNPAR 0x000004
#define INPCK 0x000010
#define INLCR 0x000040
#define IGNCR 0x000080
#define ICRNL 0x000100
#define IXON 0x000400
#define IXOFF 0x001000

/* c_oflag */
#define OPOST 0x000001
#define ONLCR 0x000004

/* c_cflag */
#define CSIZE 0x000030
#define CS5 0x000000
#define CS6 0x000010
#define CS7 0x000020
#define CS8 0x000030
#define CSTOPB 0x000040
#define CREAD 0x000080
#define PARENB 0x000100
#define CLOCAL 0x000800

/* c_lflag */
#define ISIG 0x000001
#define ICANON 0x000002
#define ECHO 0x000008
#define ECHOE 0x000010
#define ECHOK 0x000020
#define ECHONL 0x000040
#define TOSTOP 0x000100

/* baud rate indices (glibc numbering; each device maps them onto its
   own line rate) */
#define B0 0
#define B50 1
#define B75 2
#define B110 3
#define B134 4
#define B150 5
#define B200 6
#define B300 7
#define B600 8
#define B1200 9
#define B1800 10
#define B2400 11
#define B4800 12
#define B9600 13
#define B19200 14
#define B38400 15
#define B57600 16
#define B115200 17

/* ioctl requests (asm-generic numbering) */
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TCFLSH 0x540b
#define TCIFLUSH 0
#define TCOFLUSH 1
#define TCIOFLUSH 2
#define TIOCSPGRP 0x5410
#define TIOCGPGRP 0x540f
#define TIOCSCTTY 0x540e
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCGPTN 0x80045430
#define TIOCSPTLCK 0x40045431
#define TIOCGPTLCK 0x80045439

#ifdef __cplusplus
}
#endif
