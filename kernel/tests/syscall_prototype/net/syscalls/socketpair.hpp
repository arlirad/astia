#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct SocketpairHandler
        : sys::handler<SocketpairHandler, static_cast<sys::syscall_id>(81)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
