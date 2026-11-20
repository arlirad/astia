#pragma once

#include <syscall.hpp>

namespace astia::sys::syscall_prototype {
    struct ClockGettimeHandler
        : sys::handler<ClockGettimeHandler, static_cast<sys::syscall_id>(7)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
