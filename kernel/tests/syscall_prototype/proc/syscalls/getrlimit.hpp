#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct GetrlimitHandler
        : sys::handler<GetrlimitHandler, static_cast<sys::syscall_id>(48)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
