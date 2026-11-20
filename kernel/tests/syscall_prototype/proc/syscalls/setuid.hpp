#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SetuidHandler : sys::handler<SetuidHandler, static_cast<sys::syscall_id>(36)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
