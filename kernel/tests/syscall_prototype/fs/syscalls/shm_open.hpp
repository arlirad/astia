#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct ShmOpenHandler
        : sys::handler<ShmOpenHandler, static_cast<sys::syscall_id>(67)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
