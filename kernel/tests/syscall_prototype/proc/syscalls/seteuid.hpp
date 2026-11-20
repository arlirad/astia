#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SeteuidHandler
        : sys::handler<SeteuidHandler, static_cast<sys::syscall_id>(37)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
