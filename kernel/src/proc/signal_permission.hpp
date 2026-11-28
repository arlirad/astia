#pragma once

#include <astia/common.hpp>

namespace astia::proc {
    constexpr bool signal_permission(u32 sender_uid, u32 sender_euid, u32 target_uid,
                                     u32 target_euid) {
        return sender_euid == 0 || sender_uid == target_uid ||
               sender_uid == target_euid || sender_euid == target_uid ||
               sender_euid == target_euid;
    }

    constexpr bool signal_all_target_eligible(bool live, pid_t target, pid_t exclude) {
        return live && target != exclude && raw_id(target) != 1;
    }

    /// Aggregate permission decision for a signal addressed at a group or at
    /// "all processes". Only permitted members are signalled; a non-empty
    /// audience with no permitted member is EPERM, an empty one is ESRCH.
    enum class signal_audience_status {
        none,
        denied,
        allowed,
    };

    struct signal_audience {
        u32 matched   = 0;
        u32 permitted = 0;

        constexpr bool admit(u32 sender_uid, u32 sender_euid, u32 target_uid,
                             u32 target_euid) {
            ++matched;

            if (!signal_permission(sender_uid, sender_euid, target_uid, target_euid))
                return false;

            ++permitted;

            return true;
        }

        constexpr signal_audience_status status() const {
            if (matched == 0)
                return signal_audience_status::none;

            return permitted == 0 ? signal_audience_status::denied
                                  : signal_audience_status::allowed;
        }
    };
} // namespace astia::proc
