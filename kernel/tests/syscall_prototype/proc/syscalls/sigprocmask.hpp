#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SigprocmaskHandler
        : sys::handler<SigprocmaskHandler, static_cast<sys::syscall_id>(22)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
