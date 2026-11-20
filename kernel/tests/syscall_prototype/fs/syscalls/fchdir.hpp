#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct FchdirHandler : sys::handler<FchdirHandler, static_cast<sys::syscall_id>(69)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
