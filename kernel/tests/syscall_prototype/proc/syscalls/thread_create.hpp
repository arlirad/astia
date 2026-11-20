#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadCreateHandler
        : sys::handler<ThreadCreateHandler, static_cast<sys::syscall_id>(16)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
