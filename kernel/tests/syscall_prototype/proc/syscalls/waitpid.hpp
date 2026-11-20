#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_prototype {
    struct WaitpidHandler
        : sys::handler<WaitpidHandler, static_cast<sys::syscall_id>(13)> {
        static void invoke(sys::syscall_context&, core::completion<i64>& completion) {
            completion.complete(-EINPROGRESS);
        }
    };
}
