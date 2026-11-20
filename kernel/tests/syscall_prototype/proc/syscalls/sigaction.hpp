#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct SigactionHandler
        : sys::handler<SigactionHandler, static_cast<sys::syscall_id>(21)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
