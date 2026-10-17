#pragma once

#include "chrdev.hpp"
#include <astia/api/termios.h>
#include <astia/io.hpp>

// An asynchronous terminal: a character device that carries line
// discipline state.  aretev's tty minus the iopkt/packet machinery —
// the ring engine parks reads instead of queueing packets. Raw and
// canonical input processing lives here; hardware subclasses only move bytes.
// The extension points are the same: per-word flag-change hooks, and a
// speed hook that maps POSIX speed indices onto the driver's line
// rate.
//
// One deviation from aretev: a UART has a single line rate, so both
// baud directions funnel through one tty_speed() the driver
// implements, while the getters report the stored termios values.

namespace astia::dev {

    class tty : public chrdev {
        public:
        constexpr tty(const char* name, unsigned flags) : chrdev(name, flags | dv_tty) {
            // Raw console until something says otherwise: no line
            // processing, 8 data bits, carrier always present, and
            // the line at the rate the UART divisor table's default
            // (divisor 1) provides.
            _termios.c_iflag = 0;
            _termios.c_oflag = 0;
            _termios.c_cflag = CS8 | CREAD | CLOCAL;
            _termios.c_lflag = 0;
            _termios.c_line  = 0;
            for (u32 i = 0; i < NCCS; ++i) {
                _termios.c_cc[i] = 0;
            }
            _termios.c_cc[VINTR]  = 3;
            _termios.c_cc[VQUIT]  = 28;
            _termios.c_cc[VERASE] = 127;
            _termios.c_cc[VKILL]  = 21;
            _termios.c_cc[VEOF]   = 4;
            _termios.c_cc[VMIN]   = 1;
            _termios.c_cc[VSTART] = 17;
            _termios.c_cc[VSTOP]  = 19;
            _termios.c_cc[VSUSP]  = 26;
            _termios.c_ispeed     = B115200;
            _termios.c_ospeed     = B115200;
        }

        long read(void* buf, u32 len, u64 off) override;
        long write(const void* buf, u32 len, u64 off) override;
        long submit(io_request& request) override;
        long ioctl(u64 request, void* argp) override;

        protected:
        void         tty_receive(u8 byte);
        void         tty_input_ready();
        void         tty_discard_input();
        void         tty_commit_line(bool eof);
        void         tty_echo(u8 byte);
        virtual void tty_signal(u8 byte);

        /// Called per flag TCSETS changes, with the flag bit and its
        /// new state — the driver reacts without the tty layer knowing
        /// the hardware.  Default: accept silently.
        virtual void tty_iflag(tcflag_t flag, bool set);
        virtual void tty_oflag(tcflag_t flag, bool set);
        virtual void tty_cflag(tcflag_t flag, bool set);
        virtual void tty_lflag(tcflag_t flag, bool set);

        /// Apply the line rate for a POSIX speed index (both
        /// directions).  Default: speeds are unsupported (-ENOSYS,
        /// which fails the TCSETS).  A driver that can only run at one
        /// rate accepts exactly that one.
        virtual long tty_speed(speed_t speed);
        virtual bool tty_putc(u8 byte) = 0;
        virtual void tty_ready();

        struct termios       _termios;
        u32                  _column = 0; // cursor column, for OPOST work later
        static constexpr u32 rx_size = 1024;
        u8                   _rx[rx_size]{};
        u8                   _line[rx_size]{};
        u32                  _line_length    = 0;
        bool                 _output_stopped = false;
        alignas(64) u32 _rx_head             = 0;
        alignas(64) u32 _rx_tail             = 0;
    };

} // namespace astia::dev
