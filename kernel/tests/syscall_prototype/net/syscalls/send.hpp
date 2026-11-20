#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct SendHandler : sys::handler<SendHandler, static_cast<sys::syscall_id>(77)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
