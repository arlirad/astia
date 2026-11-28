#pragma once
#include <astia/api/aio.h>
#include <astia/fs.hpp>
#include <astia/span.hpp>

#include "unix_control.hpp"

namespace astia::proc {
    class process;
}

namespace astia::fs {
    long create_unix_socket(mm::shptr<open_file>& out, u32 type);
    long unix_socketpair(mm::shptr<open_file>& left, mm::shptr<open_file>& right,
                         u32 type);
} // namespace astia::fs
