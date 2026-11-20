#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadCancelHandler
        : sys::handler<ThreadCancelHandler, static_cast<sys::syscall_id>(54)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
