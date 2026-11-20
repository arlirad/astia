#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct LseekHandler : sys::handler<LseekHandler, static_cast<sys::syscall_id>(65)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
