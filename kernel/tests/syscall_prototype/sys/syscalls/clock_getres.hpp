#pragma once

#include <syscall.hpp>

namespace astia::sys::syscall_prototype {
    struct ClockGetresHandler
        : sys::handler<ClockGetresHandler, static_cast<sys::syscall_id>(62)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
