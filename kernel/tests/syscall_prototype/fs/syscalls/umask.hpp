#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct UmaskHandler : sys::handler<UmaskHandler, static_cast<sys::syscall_id>(66)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
