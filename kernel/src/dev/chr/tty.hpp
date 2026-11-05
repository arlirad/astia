#pragma once

#include "chrdev.hpp"
#include <astia/api/termios.h>
#include <astia/core/spinlock.hpp>
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

    namespace tty_defaults {
        inline constexpr cc_t ctrl_c         = 0x03;
        inline constexpr cc_t ctrl_backslash = 0x1c;
        inline constexpr cc_t del            = 0x7f;
        inline constexpr cc_t ctrl_u         = 0x15;
        inline constexpr cc_t ctrl_d         = 0x04;
        inline constexpr cc_t min_bytes      = 0x01;
        inline constexpr cc_t ctrl_q         = 0x11;
        inline constexpr cc_t ctrl_s         = 0x13;
        inline constexpr cc_t ctrl_z         = 0x1a;
    }

    class tty : public chrdev {
        public:
        constexpr tty(const char* name, unsigned flags) : chrdev(name, flags | dv_tty) {
            _termios.c_iflag = ICRNL | IXON;
            _termios.c_oflag = OPOST | ONLCR;
            _termios.c_cflag = CS8 | CREAD | CLOCAL;
            _termios.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK;
            _termios.c_line  = 0;
            for (u32 i = 0; i < NCCS; ++i) {
                _termios.c_cc[i] = 0;
            }
            _termios.c_cc[VINTR]  = tty_defaults::ctrl_c;
            _termios.c_cc[VQUIT]  = tty_defaults::ctrl_backslash;
            _termios.c_cc[VERASE] = tty_defaults::del;
            _termios.c_cc[VKILL]  = tty_defaults::ctrl_u;
            _termios.c_cc[VEOF]   = tty_defaults::ctrl_d;
            _termios.c_cc[VMIN]   = tty_defaults::min_bytes;
            _termios.c_cc[VSTART] = tty_defaults::ctrl_q;
            _termios.c_cc[VSTOP]  = tty_defaults::ctrl_s;
            _termios.c_cc[VSUSP]  = tty_defaults::ctrl_z;
            _termios.c_ispeed     = B115200;
            _termios.c_ospeed     = B115200;
        }

        long read(void* buf, u32 len, u64 off) override;
        long write(const void* buf, u32 len, u64 off) override;
        long submit(io_request& request) override;
        u32  poll(u32 events, u32 flags) override;
        long ioctl(u64 request, void* argp, usize size) override;

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

        // This lock covers the line discipline and RX ring.  It is always
        // acquired through interrupt_spinlock_guard: receive runs in IRQ
        // context, while reads/ioctls run as tasks (and SMP may race too).
        core::spinlock _state_lock;
        // TCSETS hardware hooks may take time and must not run with
        // _state_lock held.  Serialize competing termios updates separately.
        core::spinlock _ioctl_lock;

        struct termios           _termios;
        u32                      _column = 0; // cursor column, for OPOST work later
        pgrp_t                   _foreground_pgrp{};
        static constexpr bytes_t rx_capacity_bytes = 1_kib;
        u8                       _rx[rx_capacity_bytes]{};
        u8                       _line[rx_capacity_bytes]{};
        u32                      _line_length   = 0;
        bool                     _line_overflow = false;
        // Canonical VEOF at the start of a line is a real zero-length
        // read, not "not ready".  Keep that fact until the next read.
        bool _eof_pending                      = false;
        bool _output_stopped                   = false;
        alignas(cache_line_bytes) u32 _rx_head = 0;
        alignas(cache_line_bytes) u32 _rx_tail = 0;
    };

} // namespace astia::dev
