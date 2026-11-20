#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct GetcwdHandler : sys::handler<GetcwdHandler, static_cast<sys::syscall_id>(31)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
