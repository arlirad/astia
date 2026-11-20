#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_prototype {
    struct BindHandler : sys::handler<BindHandler, static_cast<sys::syscall_id>(71)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
