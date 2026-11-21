#include "tty.hpp"

#include "../../proc/proc.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>

// Host-side tty unit tests link this device in isolation.  The real kernel
// supplies these from proc.cpp; weak declarations preserve that small test
// seam without making tty invent process identities.
namespace astia::proc {
    terminal_access_result terminal_read_allowed(pgrp_t) __attribute__((weak));
    terminal_access_result terminal_write_allowed(pgrp_t, bool) __attribute__((weak));
    terminal_access_result terminal_set_foreground_allowed(pgrp_t, pgrp_t)
        __attribute__((weak));
    i64 terminal_signal_foreground(pgrp_t, signal_number) __attribute__((weak));
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

    namespace {
        long terminal_access_error(proc::terminal_access_result result,
                                   long                         denied_error) {
            switch (result) {
            case proc::terminal_access_result::allowed:
                return 0;
            case proc::terminal_access_result::io_error:
                return -EIO;
            case proc::terminal_access_result::retry:
            case proc::terminal_access_result::interrupted:
                return -EINTR;
            case proc::terminal_access_result::denied:
                return denied_error;
            }
            return denied_error;
        }
    }

    namespace {
        constexpr u64 vtime_tick_ns = 100000000;
    }

    long tty::read_available(void* buf, u32 len, u64 off, bool allow_short) {
        (void) off;
        pgrp_t foreground;
        {
            arch::interrupt_spinlock_guard guard(_state_lock);
            foreground = _foreground_pgrp;
        }
        if (proc::terminal_read_allowed != nullptr) {
            const auto access = proc::terminal_read_allowed(foreground);
            const long error  = terminal_access_error(access, -EIO);
            if (error != 0)
                return error;
        }
        arch::interrupt_spinlock_guard guard(_state_lock);
        auto*                          dst = static_cast<u8*>(buf);
        const u32 head                     = __atomic_load_n(&_rx_head, __ATOMIC_ACQUIRE);
        const u32 tail                     = _rx_tail;
        u32       count                    = (head - tail) & (rx_capacity_bytes - 1);
        if (count > len) {
            count = len;
        }
        if ((_termios.c_lflag & ICANON) != 0) {
            u32 line_count = 0;
            while (line_count < count &&
                   _rx[(tail + line_count) & (rx_capacity_bytes - 1)] != '\n') {
                ++line_count;
            }
            if (line_count < count)
                count = line_count + 1;
        }
        if (!allow_short && (_termios.c_lflag & ICANON) == 0 &&
            _termios.c_cc[VMIN] != 0 && count < _termios.c_cc[VMIN]) {
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

    long tty::read(void* buf, u32 len, u64 off) {
        return read_available(buf, len, off, false);
    }

    long tty::write(const void* buf, u32 len, u64 off) {
        (void) off;
        pgrp_t  foreground;
        bool    tostop;
        bool    output_stopped;
        termios termios;
        {
            arch::interrupt_spinlock_guard guard(_state_lock);
            foreground     = _foreground_pgrp;
            tostop         = (_termios.c_lflag & TOSTOP) != 0;
            output_stopped = _output_stopped;
            termios        = _termios;
        }
        if (proc::terminal_write_allowed != nullptr) {
            const auto access = proc::terminal_write_allowed(foreground, tostop);
            const long error  = terminal_access_error(access, -EIO);
            if (error != 0)
                return error;
        }
        if (output_stopped) {
            return 0;
        }
        const auto* source = static_cast<const u8*>(buf);
        u32         sent   = 0;
        for (; sent < len; ++sent) {
            const u8 byte = source[sent];
            if ((termios.c_oflag & OPOST) != 0 && (termios.c_oflag & ONLCR) != 0 &&
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

    long tty::submit(io::request& request) {
        termios current;
        bool    eof_pending;
        {
            arch::interrupt_spinlock_guard guard(_state_lock);
            current     = _termios;
            eof_pending = _eof_pending;
        }
        if (request.direction() == io::direction::output) {
            return write(request.buffer(), request.length(), request.offset());
        }
        // A canonical VEOF at an empty line completes a parked read with
        // zero.  Consume it here so the generic async engine does not
        // interpret zero as another "park" result.
        if ((current.c_lflag & ICANON) != 0 && eof_pending) {
            (void) read(request.buffer(), request.length(), request.offset());
            request.complete(true);
            return 0;
        }
        const bool timeout_read = request.timeout_expired();
        const long count =
            timeout_read ? read_available(request.buffer(), request.length(),
                                          request.offset(), true)
                         : read(request.buffer(), request.length(), request.offset());
        if (count > 0) {
            return count;
        }
        if ((current.c_lflag & ICANON) == 0 &&
            (timeout_read || (current.c_cc[VMIN] == 0 && current.c_cc[VTIME] == 0))) {
            request.complete(true);
            return 0;
        }
        if ((current.c_lflag & ICANON) == 0) {
            if (current.c_cc[VTIME] != 0 && request.stage() != 0 &&
                poll(POLLIN, 0) != 0) {
                request.timeout_ns(static_cast<u64>(current.c_cc[VTIME]) * vtime_tick_ns);
            }
            else if (current.c_cc[VMIN] == 0 && current.c_cc[VTIME] != 0) {
                request.timeout_ns(static_cast<u64>(current.c_cc[VTIME]) * vtime_tick_ns);
            }
        }
        request.stage(1);
        return 0;
    }

    u32 tty::poll(u32 events, u32) {
        arch::interrupt_spinlock_guard guard(_state_lock);
        const u32                      head = _rx_head;
        const u32                      tail = _rx_tail;
        const bool                     readable =
            ((head - tail) & (rx_capacity_bytes - 1)) != 0 || _eof_pending;
        u32 result = POLLOUT;
        if (readable)
            result |= POLLIN;
        return result & (events | POLLERR | POLLHUP);
    }

    bool tty::tty_receive(u8 byte) {
        arch::interrupt_spinlock_guard guard(_state_lock);
        u8                             echo[3]{};
        u32                            echo_count = 0;
        bool                           ready      = false;
        const auto                     finish     = [&]() {
            guard.unlock();
            for (u32 i = 0; i < echo_count; ++i)
                tty_echo(echo[i]);
            return ready;
        };
        if ((_termios.c_iflag & IXON) != 0) {
            if (byte == _termios.c_cc[VSTOP]) {
                _output_stopped = true;
                return false;
            }
            if (byte == _termios.c_cc[VSTART]) {
                _output_stopped = false;
                ready           = true;
                return finish();
            }
        }

        if (byte == '\r') {
            if ((_termios.c_iflag & IGNCR) != 0) {
                return false;
            }
            if ((_termios.c_iflag & ICRNL) != 0) {
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
            const u32 head = _rx_head;
            _rx_tail       = head;
            _line_length   = 0;
            _line_overflow = false;
            _eof_pending   = false;
            guard.unlock();
            tty_signal(byte);
            return false;
        }

        if ((_termios.c_lflag & ICANON) == 0) {
            const u32 head = _rx_head;
            const u32 next = (head + 1) & (rx_capacity_bytes - 1);
            if (next == _rx_tail) {
                return false;
            }
            _rx[head] = byte;
            __atomic_store_n(&_rx_head, next, __ATOMIC_RELEASE);
            if ((_termios.c_lflag & ECHO) != 0 ||
                (byte == '\n' && (_termios.c_lflag & ECHONL) != 0)) {
                echo[echo_count++] = byte;
            }
            const u32 count = (next - _rx_tail) & (rx_capacity_bytes - 1);
            ready           = _termios.c_cc[VMIN] == 0 || count >= _termios.c_cc[VMIN] ||
                    (_termios.c_cc[VTIME] != 0 && count != 0);
            return finish();
        }

        if (byte == _termios.c_cc[VERASE]) {
            if (_line_length != 0) {
                --_line_length;
                if ((_termios.c_lflag & (ECHO | ECHOE)) == (ECHO | ECHOE)) {
                    echo[echo_count++] = '\b';
                    echo[echo_count++] = ' ';
                    echo[echo_count++] = '\b';
                }
            }
            return finish();
        }
        if (byte == _termios.c_cc[VKILL]) {
            _line_length   = 0;
            _line_overflow = false;
            if ((_termios.c_lflag & (ECHO | ECHOK)) == (ECHO | ECHOK)) {
                echo[echo_count++] = '\n';
            }
            return finish();
        }
        if (byte == _termios.c_cc[VEOF]) {
            if (_line_length == 0 && !_line_overflow) {
                _eof_pending = true;
                ready        = true;
            }
            else {
                const u32 head = _rx_head;
                u32       next = head;
                for (u32 i = 0; i < _line_length; ++i) {
                    const u32 candidate = (next + 1) & (rx_capacity_bytes - 1);
                    if (candidate == _rx_tail)
                        break;
                    _rx[next] = _line[i];
                    next      = candidate;
                }
                _line_length = 0;
                _rx_head     = next;
                ready        = true;
            }
            return finish();
        }
        if (byte == '\n') {
            if (_line_overflow) {
                // A canonical line that exceeded the discipline buffer is
                // rejected as one line.  Do not leave its tail to be joined
                // with the next command (which made a later `uname` appear
                // as another suffix of the old command).
                _line_length        = 0;
                _line_overflow      = false;
                const u32 head      = _rx_head;
                u32       next      = head;
                const u32 candidate = (next + 1) & (rx_capacity_bytes - 1);
                if (candidate != _rx_tail) {
                    _rx[next] = '\n';
                    next      = candidate;
                }
                _line_length   = 0;
                _line_overflow = false;
                _rx_head       = next;
                ready          = true;
                if ((_termios.c_lflag & ECHO) != 0 || (_termios.c_lflag & ECHONL) != 0)
                    echo[echo_count++] = '\n';
                return finish();
            }
            const u32 head = _rx_head;
            u32       next = head;
            for (u32 i = 0; i < _line_length; ++i) {
                const u32 candidate = (next + 1) & (rx_capacity_bytes - 1);
                if (candidate == _rx_tail)
                    break;
                _rx[next] = _line[i];
                next      = candidate;
            }
            if (byte == '\n') {
                const u32 candidate = (next + 1) & (rx_capacity_bytes - 1);
                if (candidate != _rx_tail) {
                    _rx[next] = '\n';
                    next      = candidate;
                }
            }
            _line_length = 0;
            _rx_head     = next;
            ready        = true;
            if ((_termios.c_lflag & ECHO) != 0 || (_termios.c_lflag & ECHONL) != 0) {
                echo[echo_count++] = '\n';
            }
            return finish();
        }
        if (_line_length < rx_capacity_bytes - 1) {
            _line[_line_length++] = byte;
            if ((_termios.c_lflag & ECHO) != 0) {
                echo[echo_count++] = byte;
            }
        }
        else {
            _line_overflow = true;
        }
        return finish();
    }

    void tty::tty_input_ready() {
        tty_ready();
    }

    long tty::tty_discard_input() {
        arch::interrupt_spinlock_guard guard(_state_lock);
        const u32 head = __atomic_load_n(&_rx_head, __ATOMIC_ACQUIRE);
        __atomic_store_n(&_rx_tail, head, __ATOMIC_RELEASE);
        _line_length   = 0;
        _line_overflow = false;
        _eof_pending   = false;
        return 0;
    }

    void tty::tty_commit_line(bool eof) {
        arch::interrupt_spinlock_guard guard(_state_lock);
        const u32                      head = _rx_head;
        u32                            next = head;
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
        guard.unlock();
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
            arch::interrupt_spinlock_guard guard(_state_lock);
            *static_cast<struct termios*>(argp) = _termios;
            return 0;
        }
        if (request == TCSETS || request == TCSETSW || request == TCSETSF) {
            if (argp == nullptr || size != sizeof(struct termios)) {
                return -EINVAL;
            }
            const struct termios next = *static_cast<const struct termios*>(argp);
            arch::interrupt_spinlock_guard ioctl_guard(_ioctl_lock);
            struct termios                 old;
            {
                arch::interrupt_spinlock_guard guard(_state_lock);
                old = _termios;
            }

            // c_line is part of the ABI even though this kernel currently
            // provides only N_TTY.  Silently accepting another discipline
            // makes a userspace ABI typo particularly difficult to diagnose.
            if (next.c_line != 0 || next.c_ispeed != next.c_ospeed)
                return -EINVAL;

            // Tell the driver about every flag that moved; the rate
            // last, so a refusal leaves the line state untouched.
            for (const tcflag_t f : iflags) {
                if (((old.c_iflag ^ next.c_iflag) & f) != 0) {
                    tty_iflag(f, (next.c_iflag & f) != 0);
                }
            }
            for (const tcflag_t f : oflags) {
                if (((old.c_oflag ^ next.c_oflag) & f) != 0) {
                    tty_oflag(f, (next.c_oflag & f) != 0);
                }
            }
            for (const tcflag_t f : cflags) {
                if (((old.c_cflag ^ next.c_cflag) & f) != 0) {
                    tty_cflag(f, (next.c_cflag & f) != 0);
                }
            }
            for (const tcflag_t f : lflags) {
                if (((old.c_lflag ^ next.c_lflag) & f) != 0) {
                    tty_lflag(f, (next.c_lflag & f) != 0);
                }
            }

            const long rc = tty_speed(next.c_ospeed);
            if (rc < 0) {
                return rc;
            }
            {
                arch::interrupt_spinlock_guard guard(_state_lock);
                _termios = next;
            }
            if (request == TCSETSF) {
                const long flush_result = tty_discard_input();
                if (flush_result < 0)
                    return flush_result;
            }
            return 0;
        }
        if (request == TCFLSH) {
            if (argp == nullptr || size != sizeof(int))
                return -EINVAL;
            const int selector = *static_cast<const int*>(argp);
            if (selector == TCIFLUSH)
                return tty_discard_input();
            if (selector == TCOFLUSH || selector == TCIOFLUSH)
                return -ENOSYS;
            return -EINVAL;
        }
        if (request == TIOCGWINSZ) {
            if (argp == nullptr || size != sizeof(struct winsize))
                return -EINVAL;
            auto* size      = static_cast<struct winsize*>(argp);
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
            arch::interrupt_spinlock_guard guard(_state_lock);
            *static_cast<i32*>(argp) =
                static_cast<i32>(static_cast<u32>(_foreground_pgrp));
            return 0;
        }
        if (request == TIOCSPGRP) {
            if (argp == nullptr || size != sizeof(i32))
                return -EINVAL;
            const i32 group = *static_cast<const i32*>(argp);
            if (group <= 0)
                return -EPERM;
            if (proc::terminal_set_foreground_allowed != nullptr) {
                pgrp_t foreground;
                {
                    arch::interrupt_spinlock_guard guard(_state_lock);
                    foreground = _foreground_pgrp;
                }
                const auto access = proc::terminal_set_foreground_allowed(
                    foreground, pgrp_t{static_cast<u32>(group)});
                const long error = terminal_access_error(access, -EPERM);
                if (error != 0)
                    return error;
            }
            {
                arch::interrupt_spinlock_guard guard(_state_lock);
                _foreground_pgrp = pgrp_t{static_cast<u32>(group)};
            }
            return 0;
        }
        if (request == TIOCSCTTY)
            return argp == nullptr && size == 0 ? 0 : -EINVAL;
        return -ENOTTY;
    }

    long tty::acquire_controlling_terminal(i32 group) {
        if (group <= 0)
            return -EPERM;
        arch::interrupt_spinlock_guard guard(_state_lock);
        _foreground_pgrp = pgrp_t{static_cast<u32>(group)};
        return 0;
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
        pgrp_t  foreground;
        termios current;
        {
            arch::interrupt_spinlock_guard guard(_state_lock);
            foreground = _foreground_pgrp;
            current    = _termios;
        }
        signal_number signal = signal_number::int_;
        if (byte == current.c_cc[VQUIT])
            signal = signal_number::quit;
        else if (byte == current.c_cc[VSUSP])
            signal = signal_number::tstp;
        if (proc::terminal_signal_foreground != nullptr)
            (void) proc::terminal_signal_foreground(foreground, signal);
    }

    long tty::tty_speed(speed_t speed) {
        (void) speed;
        return -ENOSYS;
    }

} // namespace astia::dev
