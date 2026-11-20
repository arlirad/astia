#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct ConnectHandler
        : sys::handler<ConnectHandler, static_cast<sys::syscall_id>(72)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
