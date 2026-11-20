#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct IoSubmitHandler
        : sys::handler<IoSubmitHandler, static_cast<sys::syscall_id>(2)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
