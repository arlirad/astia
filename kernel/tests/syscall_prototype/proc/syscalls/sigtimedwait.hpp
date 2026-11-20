#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SigtimedwaitHandler
        : sys::handler<SigtimedwaitHandler, static_cast<sys::syscall_id>(61)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
