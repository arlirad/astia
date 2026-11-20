#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct ShutdownHandler
        : sys::handler<ShutdownHandler, static_cast<sys::syscall_id>(79)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
