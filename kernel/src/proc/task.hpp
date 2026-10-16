#pragma once

#include <astia/common.hpp>

namespace astia::proc {

    using pid_t = u32;
    using tid_t = u32;

    struct task {
        u64   stack_top;
        u64   entry;
        pid_t pid;
        tid_t tid;
    };

} // namespace astia::proc
