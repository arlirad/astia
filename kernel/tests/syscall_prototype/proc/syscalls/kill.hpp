#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct KillHandler : sys::handler<KillHandler, static_cast<sys::syscall_id>(20)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
