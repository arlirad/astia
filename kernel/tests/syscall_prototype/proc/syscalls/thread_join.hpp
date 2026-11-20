#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadJoinHandler
        : sys::handler<ThreadJoinHandler, static_cast<sys::syscall_id>(18)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
