#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadExitHandler
        : sys::handler<ThreadExitHandler, static_cast<sys::syscall_id>(17)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
