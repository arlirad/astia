#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct IoSetupHandler
        : sys::handler<IoSetupHandler, static_cast<sys::syscall_id>(0)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
