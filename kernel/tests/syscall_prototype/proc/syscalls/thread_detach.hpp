#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadDetachHandler
        : sys::handler<ThreadDetachHandler, static_cast<sys::syscall_id>(19)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
