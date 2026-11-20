#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct AcceptHandler : sys::handler<AcceptHandler, static_cast<sys::syscall_id>(70)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
