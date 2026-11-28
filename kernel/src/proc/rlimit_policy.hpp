#pragma once

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/common.hpp>

namespace astia::proc {
    /// Resource limits and kernel ceilings relevant to a setrlimit decision.
    struct rlimit_policy_state {
        astia_rlimit nofile;
        astia_rlimit address_space;
        u64          mapped_bytes = 0;
        u32          max_nofile   = 0;
    };

    /// Authorized resource change or the errno that rejects it.
    struct rlimit_policy_result {
        astia_rlimit value{};
        i64          error = 0;

        explicit operator bool() const {
            return error == 0;
        }
    };

    /// Validate a requested limit against the kernel ceilings and the caller's
    /// privilege. Unprivileged callers may not raise the hard limit; privileged
    /// callers may raise it up to the kernel ceiling.
    inline rlimit_policy_result evaluate_rlimit(i32                        resource,
                                                const astia_rlimit&        requested,
                                                const rlimit_policy_state& state,
                                                bool                       privileged) {
        if (requested.current > requested.maximum)
            return {{}, -EINVAL};

        if (resource == RLIMIT_NOFILE) {
            if (requested.maximum > state.max_nofile)
                return {{}, -EINVAL};

            if (requested.maximum > state.nofile.maximum && !privileged)
                return {{}, -EPERM};

            return {requested, 0};
        }

        if (resource == RLIMIT_AS) {
            if (requested.maximum > state.address_space.maximum && !privileged)
                return {{}, -EPERM};

            if (requested.current < state.mapped_bytes)
                return {{}, -EINVAL};

            return {requested, 0};
        }

        return {{}, -EINVAL};
    }
} // namespace astia::proc
