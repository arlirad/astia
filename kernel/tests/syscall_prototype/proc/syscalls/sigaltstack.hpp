#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SigaltstackHandler
        : sys::handler<SigaltstackHandler, static_cast<sys::syscall_id>(51)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
