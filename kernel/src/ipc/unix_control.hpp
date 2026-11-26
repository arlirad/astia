#pragma once

#include <astia/api/aio.h>
#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/mm/share.hpp>
#include <astia/span.hpp>

namespace astia::proc {
    class process;
}

namespace astia::fs {
    inline constexpr u32 unix_control_max_fds   = 16;
    inline constexpr u32 unix_control_max_bytes = 256;

    struct unix_control {
        u32                  offset{};
        u32                  count{};
        mm::shptr<open_file> files[unix_control_max_fds];
    };

    long parse_unix_control(proc::process& sender, span<const u8> control,
                            mm::shptr<unix_control>& out);
    long build_unix_control(proc::process& receiver, const unix_control& control,
                            u32 flags, span<u8> destination, u32& used, u32& msg_flags);
} // namespace astia::fs
