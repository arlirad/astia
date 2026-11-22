#include <astia/api/termios.h>

#include <stddef.h>
#include <termios.h>

_Static_assert(PARMRK == 0x000008, "PARMRK must match the termios ABI");
_Static_assert(ISTRIP == 0x000020, "ISTRIP must match the termios ABI");
_Static_assert(NOFLSH == 0x000080, "NOFLSH must match the termios ABI");
_Static_assert(IEXTEN == 0x008000, "IEXTEN must match the termios ABI");

_Static_assert(NCCS == 11, "NCCS must match the termios ABI");
_Static_assert(sizeof(tcflag_t) == 4, "tcflag_t must match the termios ABI");
_Static_assert(sizeof(cc_t) == 1, "cc_t must match the termios ABI");
_Static_assert(sizeof(speed_t) == 4, "speed_t must match the termios ABI");

_Static_assert(offsetof(struct termios, c_iflag) == 0,
               "c_iflag must start struct termios");
_Static_assert(offsetof(struct termios, c_oflag) == 4, "c_oflag must follow c_iflag");
_Static_assert(offsetof(struct termios, c_cflag) == 8, "c_cflag must follow c_oflag");
_Static_assert(offsetof(struct termios, c_lflag) == 12, "c_lflag must follow c_cflag");
_Static_assert(offsetof(struct termios, c_line) == 16, "c_line must follow c_lflag");
_Static_assert(offsetof(struct termios, c_cc) == 17, "c_cc must follow c_line");
_Static_assert(offsetof(struct termios, c_ispeed) == 28, "c_ispeed must follow c_cc");
_Static_assert(offsetof(struct termios, c_ospeed) == 32, "c_ospeed must follow c_ispeed");
_Static_assert(sizeof(struct termios) == 36, "struct termios must match the termios ABI");

int main(void) {
    return 0;
}
