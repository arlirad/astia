#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GetuidHandler : sys::handler<GetuidHandler, static_cast<sys::syscall_id>(32)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
