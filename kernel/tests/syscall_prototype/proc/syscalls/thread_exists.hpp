#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadExistsHandler
        : sys::handler<ThreadExistsHandler, static_cast<sys::syscall_id>(58)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
