#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_prototype {
    struct ChdirHandler : sys::handler<ChdirHandler, static_cast<sys::syscall_id>(30)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
