#pragma once

#include <astia/common.hpp>

namespace astia::fs::syscall {
    i64 io_submit(u64 ring, u32 submit);
}
