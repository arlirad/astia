#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GetegidHandler
        : sys::handler<GetegidHandler, static_cast<sys::syscall_id>(35)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
