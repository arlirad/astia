#include "chr/tty.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    namespace {
        // Flags the tty layer knows about, per mode word: TCSETS walks
        // these to tell the driver only about bits that actually
        // moved.
        constexpr tcflag_t iflags[] = { IGNBRK, BRKINT, IGNPAR, INPCK,
                                        ICRNL, IXON, IXOFF };
        constexpr tcflag_t oflags[] = { OPOST, ONLCR };
        constexpr tcflag_t cflags[] = { CSIZE, CSTOPB, PARENB, CREAD,
                                        CLOCAL };
        constexpr tcflag_t lflags[] = { ISIG, ICANON, ECHO, ECHOE,
                                        ECHOK, ECHONL };
    } // namespace

    auto tty::ioctl(u64 request, void* argp) -> long {
        if (request == TCGETS) {
            if (argp == nullptr) {
                return -EINVAL;
            }
            *static_cast<struct termios*>(argp) = _termios;
            return 0;
        }
        if (request == TCSETS) {
            if (argp == nullptr) {
                return -EINVAL;
            }
            const struct termios& next =
                *static_cast<const struct termios*>(argp);

            // Tell the driver about every flag that moved; the rate
            // last, so a refusal leaves the line state untouched.
            for (const tcflag_t f : iflags) {
                if (((_termios.c_iflag ^ next.c_iflag) & f) != 0) {
                    tty_iflag(f, (next.c_iflag & f) != 0);
                }
            }
            for (const tcflag_t f : oflags) {
                if (((_termios.c_oflag ^ next.c_oflag) & f) != 0) {
                    tty_oflag(f, (next.c_oflag & f) != 0);
                }
            }
            for (const tcflag_t f : cflags) {
                if (((_termios.c_cflag ^ next.c_cflag) & f) != 0) {
                    tty_cflag(f, (next.c_cflag & f) != 0);
                }
            }
            for (const tcflag_t f : lflags) {
                if (((_termios.c_lflag ^ next.c_lflag) & f) != 0) {
                    tty_lflag(f, (next.c_lflag & f) != 0);
                }
            }

            const long rc = tty_speed(next.c_ospeed);
            if (rc < 0) {
                return rc;
            }
            _termios = next;
            return 0;
        }
        return -ENOTTY;
    }

    void tty::tty_iflag(tcflag_t flag, bool set) {
        (void) flag;
        (void) set;
    }

    void tty::tty_oflag(tcflag_t flag, bool set) {
        (void) flag;
        (void) set;
    }

    void tty::tty_cflag(tcflag_t flag, bool set) {
        (void) flag;
        (void) set;
    }

    void tty::tty_lflag(tcflag_t flag, bool set) {
        (void) flag;
        (void) set;
    }

    auto tty::tty_speed(speed_t speed) -> long {
        (void) speed;
        return -ENOSYS;
    }

} // namespace astia::dev
