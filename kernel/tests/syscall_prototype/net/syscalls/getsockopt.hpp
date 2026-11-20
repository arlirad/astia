#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct GetsockoptHandler
        : sys::handler<GetsockoptHandler, static_cast<sys::syscall_id>(73)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
