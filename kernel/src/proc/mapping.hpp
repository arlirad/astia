#pragma once

#include "proc.hpp"
#include <astia/mapping.hpp>

namespace astia {

    template <>
    struct mapper<proc::getsid_target> {
        static optional<proc::getsid_target> from(i64 raw) {
            // The subsystem handler translates this rejected selector to
            // native ESRCH; the generic kernel error vocabulary has no
            // separate lookup-miss value.
            if (raw < 0 || raw > 0xffffffffll)
                return optional<proc::getsid_target>(error_t::einval);
            return optional<proc::getsid_target>(
                proc::getsid_target{pid_from_raw(static_cast<u32>(raw))});
        }
    };

    template <>
    struct mapper<signal_number> {
        static optional<signal_number> from(i64 raw) {
            if (raw < 0 || raw >= static_cast<i64>(signal_count) ||
                !valid_signal(static_cast<signal_number>(raw)))
                return optional<signal_number>(error_t::einval);
            return optional<signal_number>(static_cast<signal_number>(raw));
        }
    };

    template <>
    struct mapper<signal_mask_operation> {
        static optional<signal_mask_operation> from(i64 raw) {
            if (!valid_signal_mask_operation(raw))
                return optional<signal_mask_operation>(error_t::einval);
            return optional<signal_mask_operation>(
                static_cast<signal_mask_operation>(raw));
        }
    };

    template <>
    struct mapper<proc::wait_options>
        : bitmask_mapper<proc::wait_options, proc::valid_wait_option_bits> {};

    template <>
    struct mapper<pid_t> {
        static optional<pid_t> from(i64 raw) {
            if (raw <= 0 || raw > 0xffffffffll)
                return optional<pid_t>(error_t::einval);
            return optional<pid_t>(pid_from_raw(static_cast<u32>(raw)));
        }
    };

    template <>
    struct mapper<pgrp_t> {
        static optional<pgrp_t> from(i64 raw) {
            if (raw <= 0 || raw > 0xffffffffll)
                return optional<pgrp_t>(error_t::einval);
            return optional<pgrp_t>(pgrp_from_raw(static_cast<u32>(raw)));
        }
    };

    template <>
    struct mapper<fd_t> {
        static optional<fd_t> from(i64 raw) {
            if (raw < 0 || raw >= static_cast<i64>(proc::max_process_fds))
                return optional<fd_t>(error_t::ebadf);
            return optional<fd_t>(fd_t{static_cast<i32>(raw)});
        }
    };

    template <>
    struct mapper<uid_t> {
        static optional<uid_t> from(i64 raw) {
            if (raw < 0 || raw > 0xffffffffll)
                return optional<uid_t>(error_t::einval);
            return optional<uid_t>(uid_t{static_cast<u32>(raw)});
        }
    };
    template <>
    struct mapper<gid_t> {
        static optional<gid_t> from(i64 raw) {
            if (raw < 0 || raw > 0xffffffffll)
                return optional<gid_t>(error_t::einval);
            return optional<gid_t>(gid_t{static_cast<u32>(raw)});
        }
    };

    template <>
    struct mapper<proc::map_protection>
        : bitmask_mapper<proc::map_protection, proc::valid_map_protection_bits> {};

    template <>
    struct mapper<proc::map_flags>
        : bitmask_mapper<proc::map_flags, proc::valid_map_flag_bits> {};

} // namespace astia
