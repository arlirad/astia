#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ForkHandler : sys::handler<ForkHandler, static_cast<sys::syscall_id>(15)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
