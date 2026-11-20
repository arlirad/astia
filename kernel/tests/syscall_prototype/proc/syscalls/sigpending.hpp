#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SigpendingHandler
        : sys::handler<SigpendingHandler, static_cast<sys::syscall_id>(60)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
