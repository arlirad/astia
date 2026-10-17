#pragma once

#include <astia/common.hpp>

namespace astia {

    /// POSIX signal numbers shared by the kernel and userspace ABI.
    enum class signal_number : u8 {
        hup    = 1,
        int_   = 2,
        quit   = 3,
        ill    = 4,
        trap   = 5,
        abrt   = 6,
        bus    = 7,
        fpe    = 8,
        kill   = 9,
        usr1   = 10,
        segv   = 11,
        usr2   = 12,
        pipe   = 13,
        alrm   = 14,
        term   = 15,
        chld   = 17,
        cont   = 18,
        stop   = 19,
        tstp   = 20,
        ttin   = 21,
        ttou   = 22,
        urg    = 23,
        xcpu   = 24,
        xfsz   = 25,
        vtalrm = 26,
        prof   = 27,
        winch  = 28,
        io     = 29,
        pwr    = 30,
        sys    = 31,
    };

    inline constexpr u32 signal_count = 32;
    using signal_mask                 = u32;
    using signal_handler              = void (*)(int);

    enum class signal_action : u8 {
        default_action,
        ignore,
        handler,
    };

    enum class signal_default : u8 {
        ignore,
        terminate,
        core,
        stop,
        continue_ = 5,
    };

    struct signal_disposition {
        signal_action  action  = signal_action::default_action;
        signal_handler handler = nullptr;
    };

    constexpr bool valid_signal(signal_number signal) {
        const u8 value = static_cast<u8>(signal);
        return value > 0 && value < signal_count;
    }

    constexpr signal_mask signal_bit(signal_number signal) {
        return valid_signal(signal) ? (signal_mask{1} << static_cast<u8>(signal)) : 0;
    }

    constexpr bool is_uncatchable(signal_number signal) {
        return signal == signal_number::kill || signal == signal_number::stop;
    }

    constexpr signal_default default_action(signal_number signal) {
        switch (signal) {
        case signal_number::chld:
        case signal_number::cont:
        case signal_number::urg:
        case signal_number::winch:
        case signal_number::io:
            return signal_default::ignore;
        case signal_number::stop:
        case signal_number::tstp:
        case signal_number::ttin:
        case signal_number::ttou:
            return signal_default::stop;
        case signal_number::quit:
        case signal_number::ill:
        case signal_number::abrt:
        case signal_number::bus:
        case signal_number::fpe:
        case signal_number::segv:
        case signal_number::sys:
            return signal_default::core;
        case signal_number::hup:
        case signal_number::int_:
        case signal_number::trap:
        case signal_number::kill:
        case signal_number::usr1:
        case signal_number::usr2:
        case signal_number::pipe:
        case signal_number::alrm:
        case signal_number::term:
        case signal_number::xcpu:
        case signal_number::xfsz:
        case signal_number::vtalrm:
        case signal_number::prof:
        case signal_number::pwr:
            return signal_default::terminate;
        }
        return signal_default::terminate;
    }

    /// Per-process signal state. Sending and selecting signals are pure
    /// kernel policy; architecture-specific delivery is a later layer.
    class signal_state {
        public:
        bool send(signal_number signal) {
            if (!valid_signal(signal)) {
                return false;
            }
            if (!is_uncatchable(signal) &&
                dispositions_[static_cast<u8>(signal)].action == signal_action::ignore) {
                return true;
            }
            pending_ |= signal_bit(signal);
            return true;
        }

        signal_number take_unblocked() {
            const signal_mask available = pending_ & ~blocked_;
            if (available == 0) {
                return static_cast<signal_number>(0);
            }
            for (u8 value = 1; value < signal_count; ++value) {
                if ((available & (signal_mask{1} << value)) != 0) {
                    pending_ &= ~(signal_mask{1} << value);
                    return static_cast<signal_number>(value);
                }
            }
            return static_cast<signal_number>(0);
        }

        void block(signal_mask mask) {
            blocked_ |= mask;
            blocked_ &=
                ~(signal_bit(signal_number::kill) | signal_bit(signal_number::stop));
        }

        void unblock(signal_mask mask) {
            blocked_ &= ~mask;
        }

        void set_blocked(signal_mask mask) {
            blocked_ = mask & ~(signal_bit(signal_number::kill) |
                                signal_bit(signal_number::stop));
        }

        signal_mask blocked() const {
            return blocked_;
        }
        signal_mask pending() const {
            return pending_;
        }

        signal_disposition disposition(signal_number signal) const {
            return valid_signal(signal) ? dispositions_[static_cast<u8>(signal)]
                                        : signal_disposition{};
        }

        bool disposition(signal_number signal, signal_disposition value) {
            if (!valid_signal(signal) || is_uncatchable(signal)) {
                return false;
            }
            dispositions_[static_cast<u8>(signal)] = value;
            return true;
        }

        bool has_unblocked_pending() const {
            return (pending_ & ~blocked_) != 0;
        }

        private:
        signal_disposition dispositions_[signal_count]{};
        signal_mask        pending_ = 0;
        signal_mask        blocked_ = 0;
    };

} // namespace astia
