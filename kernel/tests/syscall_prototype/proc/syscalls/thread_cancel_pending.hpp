#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadCancelPendingHandler
        : sys::handler<ThreadCancelPendingHandler, static_cast<sys::syscall_id>(57)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
