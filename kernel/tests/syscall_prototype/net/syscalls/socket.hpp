#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct SocketHandler : sys::handler<SocketHandler, static_cast<sys::syscall_id>(80)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
