#pragma once

#include <astia/common.hpp>

namespace astia::proc {

    constexpr bool signal_permission(u32 sender_uid, u32 sender_euid, u32 target_uid,
                                     u32 target_euid) {
        return sender_euid == 0 || sender_uid == target_uid ||
               sender_uid == target_euid || sender_euid == target_uid ||
               sender_euid == target_euid;
    }

} // namespace astia::proc
