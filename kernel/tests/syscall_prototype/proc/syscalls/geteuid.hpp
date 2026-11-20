#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GeteuidHandler
        : sys::handler<GeteuidHandler, static_cast<sys::syscall_id>(33)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
