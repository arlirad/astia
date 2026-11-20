#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct YieldHandler : sys::handler<YieldHandler, static_cast<sys::syscall_id>(9)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
