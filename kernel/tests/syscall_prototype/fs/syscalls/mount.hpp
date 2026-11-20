#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct MountHandler : sys::handler<MountHandler, static_cast<sys::syscall_id>(47)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
