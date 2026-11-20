#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct ThreadKillHandler
        : sys::handler<ThreadKillHandler, static_cast<sys::syscall_id>(59)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
