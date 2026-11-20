#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ExecveHandler : sys::handler<ExecveHandler, static_cast<sys::syscall_id>(24)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
