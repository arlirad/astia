#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GetpgidHandler
        : sys::handler<GetpgidHandler, static_cast<sys::syscall_id>(52)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
