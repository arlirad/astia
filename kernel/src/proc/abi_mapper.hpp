#pragma once

#include "proc.hpp"

namespace astia::proc::abi {

    template <typename T>
    struct mapper;

    template <>
    struct mapper<signal_t> {
        static optional<signal_t> from(i64 raw) {
            if (raw < 0 || raw >= static_cast<i64>(signal_count))
                return optional<signal_t>(error_t::einval);
            return optional<signal_t>(static_cast<signal_t>(raw));
        }
    };

    template <>
    struct mapper<wait_options> {
        static optional<wait_options> from(u64 raw) {
            constexpr u32 valid = WNOHANG | WUNTRACED | WCONTINUED;
            if (raw & ~static_cast<u64>(valid))
                return optional<wait_options>(error_t::einval);
            return optional<wait_options>(static_cast<wait_options>(raw));
        }
    };

    template <>
    struct mapper<pid_t> {
        static optional<pid_t> from(i64 raw) {
            if (raw <= 0 || raw > 0xffffffffll)
                return optional<pid_t>(error_t::einval);
            return optional<pid_t>(static_cast<pid_t>(raw));
        }
    };

    template <>
    struct mapper<fd_t> {
        static optional<fd_t> from(i64 raw) {
            if (raw < 0 || raw >= static_cast<i64>(max_process_fds))
                return optional<fd_t>(error_t::ebadf);
            return optional<fd_t>(static_cast<fd_t>(raw));
        }
    };

} // namespace astia::proc::abi
