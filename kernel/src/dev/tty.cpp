#include "chr/tty.hpp"

#include "../sys/aio.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>

namespace astia::dev {

    namespace {
        // Flags the tty layer knows about, per mode word: TCSETS walks
        // these to tell the driver only about bits that actually
        // moved.
        constexpr tcflag_t iflags[] = {IGNBRK, BRKINT, IGNPAR, INPCK, ICRNL, IXON, IXOFF};
        constexpr tcflag_t oflags[] = {OPOST, ONLCR};
        constexpr tcflag_t cflags[] = {CSIZE, CSTOPB, PARENB, CREAD, CLOCAL};
        constexpr tcflag_t lflags[] = {ISIG, ICANON, ECHO, ECHOE, ECHOK, ECHONL};
    } // namespace

    long tty::read(void* buf, u32 len, u64 off) {
        (void) off;
        arch::interrupt_guard gate;
        auto*                 dst   = static_cast<u8*>(buf);
        const u32             head  = __atomic_load_n(&_rx_head, __ATOMIC_ACQUIRE);
        const u32             tail  = _rx_tail;
        u32                   count = (head - tail) & (rx_size - 1);
        if (count > len) {
            count = len;
        }
        for (u32 i = 0; i < count; ++i) {
            dst[i] = _rx[(tail + i) & (rx_size - 1)];
        }
        __atomic_store_n(&_rx_tail, (tail + count) & (rx_size - 1), __ATOMIC_RELEASE);
        return static_cast<long>(count);
    }

    long tty::write(const void* buf, u32 len, u64 off) {
        (void) off;
        const auto* source = static_cast<const u8*>(buf);
        u32         sent   = 0;
        for (; sent < len; ++sent) {
            const u8 byte = source[sent];
            if ((_termios.c_oflag & OPOST) != 0 && (_termios.c_oflag & ONLCR) != 0 &&
                byte == '\n') {
                if (!tty_putc('\r') || !tty_putc('\n')) {
                    break;
                }
            }
            else if (!tty_putc(byte)) {
                break;
            }
        }
        return static_cast<long>(sent);
    }

    long tty::submit(io_request& request) {
        if (request.direction() == io_direction::output) {
            return write(request.buffer(), request.length(), request.offset());
        }
        const long count = read(request.buffer(), request.length(), request.offset());
        if (count > 0) {
            return count;
        }
        request.stage(1);
        return 0;
    }

    void tty::tty_receive(u8 byte) {
        const u32 head = _rx_head;
        const u32 next = (head + 1) & (rx_size - 1);
        if (next == _rx_tail) {
            return;
        }
        _rx[head] = byte;
        __atomic_store_n(&_rx_head, next, __ATOMIC_RELEASE);
    }

    void tty::tty_input_ready() {
        tty_ready();
    }

    void tty::tty_discard_input() {
        arch::interrupt_guard gate;
        const u32             head = __atomic_load_n(&_rx_head, __ATOMIC_ACQUIRE);
        __atomic_store_n(&_rx_tail, head, __ATOMIC_RELEASE);
    }

    long tty::ioctl(u64 request, void* argp) {
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
            const struct termios& next = *static_cast<const struct termios*>(argp);

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

    void tty::tty_ready() {}

    long tty::tty_speed(speed_t speed) {
        (void) speed;
        return -ENOSYS;
    }

} // namespace astia::dev
