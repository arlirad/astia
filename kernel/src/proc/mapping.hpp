#pragma once

#include "proc.hpp"
#include <astia/api/aio.h>
#include <astia/mapping.hpp>

namespace astia::mapping {

    template <>
    struct mapper<signal_t> {
        static optional<signal_t> from(i64 raw) {
            if (raw < 0 || raw >= static_cast<i64>(signal_count))
                return optional<signal_t>(error_t::einval);
            return optional<signal_t>(static_cast<signal_t>(raw));
        }
    };

    template <>
    struct mapper<proc::wait_options>
        : bitmask_mapper<proc::wait_options,
                         WNOHANG | WUNTRACED | WCONTINUED> {};

    template <>
    struct mapper<proc::pid_t> {
        static optional<proc::pid_t> from(i64 raw) {
            if (raw <= 0 || raw > 0xffffffffll)
                return optional<proc::pid_t>(error_t::einval);
            return optional<proc::pid_t>(static_cast<proc::pid_t>(raw));
        }
    };

    template <>
    struct mapper<fd_t> {
        static optional<fd_t> from(i64 raw) {
            if (raw < 0 || raw >= static_cast<i64>(proc::max_process_fds))
                return optional<fd_t>(error_t::ebadf);
            return optional<fd_t>(static_cast<fd_t>(raw));
        }
    };

} // namespace astia::mapping
