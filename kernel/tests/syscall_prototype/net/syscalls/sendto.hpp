#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct SendtoHandler : sys::handler<SendtoHandler, static_cast<sys::syscall_id>(78)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
