#include "tty.hpp"

#include "../../proc/proc.hpp"
#include "../../sys/aio.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/log.hpp>

// Host-side tty unit tests link this device in isolation.  The real kernel
// supplies these from proc.cpp; weak declarations preserve that small test
// seam without making tty invent process identities.
namespace astia::proc {
    bool terminal_read_allowed(pgrp_t) __attribute__((weak));
    bool terminal_write_allowed(pgrp_t, bool) __attribute__((weak));
    bool terminal_set_foreground_allowed(pgrp_t) __attribute__((weak));
    i64  terminal_signal_foreground(pgrp_t, signal_number) __attribute__((weak));
}

namespace astia::dev {

    namespace {
        // Flags the tty layer knows about, per mode word: TCSETS walks
        // these to tell the driver only about bits that actually
        // moved.
        constexpr tcflag_t iflags[] = {IGNBRK, BRKINT, IGNPAR, INPCK, INLCR,
                                       IGNCR,  ICRNL,  IXON,   IXOFF};
        constexpr tcflag_t oflags[] = {OPOST, ONLCR};
        constexpr tcflag_t cflags[] = {CSIZE, CSTOPB, PARENB, CREAD, CLOCAL};
        constexpr tcflag_t lflags[] = {ISIG, ICANON, ECHO, ECHOE, ECHOK, ECHONL};
    } // namespace

    long tty::read(void* buf, u32 len, u64 off) {
        (void) off;
        if (proc::terminal_read_allowed != nullptr &&
            !proc::terminal_read_allowed(_foreground_pgrp))
            return -EIO;
        arch::interrupt_guard gate;
        auto*                 dst   = static_cast<u8*>(buf);
        const u32             head  = __atomic_load_n(&_rx_head, __ATOMIC_ACQUIRE);
        const u32             tail  = _rx_tail;
        u32                   count = (head - tail) & (rx_capacity_bytes - 1);
        if (count > len) {
            count = len;
        }
        if ((_termios.c_lflag & ICANON) == 0 && _termios.c_cc[VMIN] != 0 &&
            count < _termios.c_cc[VMIN]) {
            count = 0;
        }
        if ((_termios.c_lflag & ICANON) != 0 && count == 0 && _eof_pending) {
            _eof_pending = false;
            return 0;
        }
        for (u32 i = 0; i < count; ++i) {
            dst[i] = _rx[(tail + i) & (rx_capacity_bytes - 1)];
        }
        __atomic_store_n(&_rx_tail, (tail + count) & (rx_capacity_bytes - 1),
                         __ATOMIC_RELEASE);
        return static_cast<long>(count);
    }

    long tty::write(const void* buf, u32 len, u64 off) {
        (void) off;
        if (proc::terminal_write_allowed != nullptr &&
            !proc::terminal_write_allowed(_foreground_pgrp,
                                          (_termios.c_lflag & TOSTOP) != 0))
            return -EIO;
        if (_output_stopped) {
            return 0;
        }
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
        // A canonical VEOF at an empty line completes a parked read with
        // zero.  Consume it here so the generic async engine does not
        // interpret zero as another "park" result.
        if ((_termios.c_lflag & ICANON) != 0 && _eof_pending) {
            (void) read(request.buffer(), request.length(), request.offset());
            request.complete(true);
            return 0;
        }
        const long count = read(request.buffer(), request.length(), request.offset());
        if (count > 0) {
            return count;
        }
        if ((_termios.c_lflag & ICANON) == 0 && _termios.c_cc[VMIN] == 0) {
            request.complete(true);
            return 0;
        }
        request.stage(1);
        return 0;
    }

    u32 tty::poll(u32 events, u32) {
        const u32  head = __atomic_load_n(&_rx_head, __ATOMIC_ACQUIRE);
        const u32  tail = __atomic_load_n(&_rx_tail, __ATOMIC_RELAXED);
        const bool readable =
            ((head - tail) & (rx_capacity_bytes - 1)) != 0 || _eof_pending;
        u32 result = POLLOUT;
        if (readable)
            result |= POLLIN;
        return result & (events | POLLERR | POLLHUP);
    }

    void tty::tty_receive(u8 byte) {
        if ((_termios.c_iflag & IXON) != 0) {
            if (byte == _termios.c_cc[VSTOP]) {
                _output_stopped = true;
                return;
            }
            if (byte == _termios.c_cc[VSTART]) {
                _output_stopped = false;
                tty_input_ready();
                return;
            }
        }

        if (byte == '\r') {
            if ((_termios.c_iflag & IGNCR) != 0) {
                return;
            }
            /* Serial consoles commonly deliver Enter as CR.  Keep that
               key useful after a raw-mode program (notably zsh) clears
               ICRNL: zsh binds both CR and LF to accept-line, while a
               bare CR otherwise only moves the host cursor to column zero. */
            if ((_termios.c_iflag & ICRNL) != 0 || (_termios.c_lflag & ICANON) == 0) {
                byte = '\n';
            }
        }
        else if (byte == '\n' && (_termios.c_iflag & INLCR) != 0) {
            byte = '\r';
        }

        if ((_termios.c_lflag & ISIG) != 0 &&
            (byte == _termios.c_cc[VINTR] || byte == _termios.c_cc[VQUIT] ||
             byte == _termios.c_cc[VSUSP])) {
            // VINTR/VQUIT/VSUSP discard the current input line.  In
            // particular, Ctrl-C must not leave the half typed command to
            // be consumed after the foreground job handles the signal.
            tty_discard_input();
            tty_signal(byte);
            return;
        }

        if ((_termios.c_lflag & ICANON) == 0) {
            const u32 head = _rx_head;
            const u32 next = (head + 1) & (rx_capacity_bytes - 1);
            if (next == _rx_tail) {
                return;
            }
            _rx[head] = byte;
            __atomic_store_n(&_rx_head, next, __ATOMIC_RELEASE);
            tty_echo(byte);
            return;
        }

        if (byte == _termios.c_cc[VERASE]) {
            if (_line_length != 0) {
                --_line_length;
                if ((_termios.c_lflag & ECHOE) != 0) {
                    tty_echo('\b');
                    tty_echo(' ');
                    tty_echo('\b');
                }
            }
            return;
        }
        if (byte == _termios.c_cc[VKILL]) {
            _line_length   = 0;
            _line_overflow = false;
            if ((_termios.c_lflag & ECHOK) != 0) {
                tty_echo('\n');
            }
            return;
        }
        if (byte == _termios.c_cc[VEOF]) {
            if (_line_length == 0 && !_line_overflow) {
                _eof_pending = true;
                tty_input_ready();
            }
            else {
                tty_commit_line(true);
            }
            return;
        }
        if (byte == '\n') {
            if (_line_overflow) {
                // A canonical line that exceeded the discipline buffer is
                // rejected as one line.  Do not leave its tail to be joined
                // with the next command (which made a later `uname` appear
                // as another suffix of the old command).
                _line_length   = 0;
                _line_overflow = false;
                tty_commit_line(false);
                if ((_termios.c_lflag & ECHO) != 0 || (_termios.c_lflag & ECHONL) != 0)
                    tty_echo('\n');
                return;
            }
            tty_commit_line(false);
            if ((_termios.c_lflag & ECHO) != 0 || (_termios.c_lflag & ECHONL) != 0) {
                tty_echo('\n');
            }
            return;
        }
        if (_line_length < rx_capacity_bytes - 1) {
            _line[_line_length++] = byte;
            if ((_termios.c_lflag & ECHO) != 0) {
                tty_echo(byte);
            }
        }
        else {
            _line_overflow = true;
        }
    }

    void tty::tty_input_ready() {
        tty_ready();
    }

    void tty::tty_discard_input() {
        arch::interrupt_guard gate;
        const u32             head = __atomic_load_n(&_rx_head, __ATOMIC_ACQUIRE);
        __atomic_store_n(&_rx_tail, head, __ATOMIC_RELEASE);
        _line_length   = 0;
        _line_overflow = false;
        _eof_pending   = false;
    }

    void tty::tty_commit_line(bool eof) {
        const u32 head = _rx_head;
        u32       next = head;
        for (u32 i = 0; i < _line_length; ++i) {
            const u32 candidate = (next + 1) & (rx_capacity_bytes - 1);
            if (candidate == _rx_tail) {
                break;
            }
            _rx[next] = _line[i];
            next      = candidate;
        }
        if (!eof) {
            const u32 candidate = (next + 1) & (rx_capacity_bytes - 1);
            if (candidate != _rx_tail) {
                _rx[next] = '\n';
                next      = candidate;
            }
        }
        _line_length = 0;
        __atomic_store_n(&_rx_head, next, __ATOMIC_RELEASE);
        tty_input_ready();
    }

    void tty::tty_echo(u8 byte) {
        (void) tty_putc(byte);
    }

    long tty::ioctl(u64 request, void* argp, usize size) {
        if (request == TCGETS) {
            if (argp == nullptr || size != sizeof(struct termios)) {
                return -EINVAL;
            }
            *static_cast<struct termios*>(argp) = _termios;
            return 0;
        }
        if (request == TCSETS || request == TCSETSW || request == TCSETSF) {
            if (argp == nullptr || size != sizeof(struct termios)) {
                return -EINVAL;
            }
            const struct termios& next = *static_cast<const struct termios*>(argp);

            // c_line is part of the ABI even though this kernel currently
            // provides only N_TTY.  Silently accepting another discipline
            // makes a userspace ABI typo particularly difficult to diagnose.
            if (next.c_line != 0 || next.c_ispeed != next.c_ospeed)
                return -EINVAL;

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
            if (request == TCSETSF)
                tty_discard_input();
            return 0;
        }
        if (request == TIOCGWINSZ) {
            if (argp == nullptr || size != sizeof(struct winsize))
                return -EINVAL;
            auto* size = static_cast<struct winsize*>(argp);
            size->ws_row    = 24;
            size->ws_col    = 80;
            size->ws_xpixel = 0;
            size->ws_ypixel = 0;
            return 0;
        }
        if (request == TIOCSWINSZ) {
            return argp == nullptr || size != sizeof(struct winsize) ? -EINVAL : 0;
        }
        if (request == TIOCGPGRP) {
            if (argp == nullptr || size != sizeof(i32))
                return -EINVAL;
            *static_cast<i32*>(argp) =
                static_cast<i32>(static_cast<u32>(_foreground_pgrp));
            return 0;
        }
        if (request == TIOCSPGRP) {
            if (argp == nullptr || size != sizeof(i32))
                return -EINVAL;
            const i32 group = *static_cast<const i32*>(argp);
            if (group <= 0 ||
                (proc::terminal_set_foreground_allowed != nullptr &&
                 !proc::terminal_set_foreground_allowed(pgrp_t{static_cast<u32>(group)})))
                return -EPERM;
            _foreground_pgrp = pgrp_t{static_cast<u32>(group)};
            return 0;
        }
        if (request == TIOCSCTTY)
            return argp == nullptr && size == 0 ? 0 : -EINVAL;
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

    void tty::tty_signal(u8 byte) {
        signal_number signal = signal_number::int_;
        if (byte == _termios.c_cc[VQUIT])
            signal = signal_number::quit;
        else if (byte == _termios.c_cc[VSUSP])
            signal = signal_number::tstp;
        if (proc::terminal_signal_foreground != nullptr)
            (void) proc::terminal_signal_foreground(_foreground_pgrp, signal);
    }

    long tty::tty_speed(speed_t speed) {
        (void) speed;
        return -ENOSYS;
    }

} // namespace astia::dev
