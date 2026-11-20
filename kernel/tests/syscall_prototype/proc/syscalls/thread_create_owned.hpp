#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadCreateOwnedHandler
        : sys::handler<ThreadCreateOwnedHandler, static_cast<sys::syscall_id>(56)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
